/*** includes ***/
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>

/*** custom defines ***/
#define CTRL_KEY(k) ((k) & 0x1f)
#define CURRENT_VERSION "0.0.1"

/*** data ***/
typedef struct editrow
{
    int size;
    char *chars;
} editrow;

struct editorConfig
{
    struct termios original_term_mode;
    int screen_rows;
    int screen_cols;

    int cx;
    int cy;
    int row_offset;
    int col_offset;

    int numrows;
    editrow *rows;

    char *file_name;
};
struct editorConfig edit_conf;

typedef struct cache_buffer
{
    char *cbuffer;
    int len;
} cache_buffer;

#define CBUFFER_INIT \
    {                \
        NULL, 0      \
    }

enum editorKey
{
    BACKSPACE = 127,
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    PAGE_UP,
    PAGE_DOWN,
    DEL_KEY,
};

/*** functions ***/

void editorInsertRow(char *s, size_t len, int pos)
{
    if (pos < 0 || pos > edit_conf.numrows)
        return;
    edit_conf.rows = realloc(edit_conf.rows, sizeof(editrow) * (edit_conf.numrows + 1));
    memmove(&edit_conf.rows[pos + 1], &edit_conf.rows[pos], sizeof(editrow) * (edit_conf.numrows - pos));

    edit_conf.rows[pos].size = len;
    edit_conf.rows[pos].chars = malloc(len + 1);
    memcpy(edit_conf.rows[pos].chars, s, len);
    edit_conf.rows[pos].chars[len] = '\0';
    edit_conf.numrows++;
}

void editorInsertNewline()
{
    int line_num = edit_conf.cy + edit_conf.row_offset;
    if (edit_conf.cx == 0)
    {
        editorInsertRow("", 0, line_num);
    }
    else
    {
        editrow *row = &edit_conf.rows[line_num];
        editorInsertRow(&row->chars[edit_conf.cx], row->size - edit_conf.cx, line_num + 1);
        row = &edit_conf.rows[line_num];
        row->size = edit_conf.cx;
        row->chars[row->size] = '\0';
    }
    edit_conf.cy++;
    edit_conf.cx = 0;
}

void editorRowInsertChar(editrow *row, int position, int chr)
{
    if (position < 0 || position > row->size)
        position = row->size;
    row->chars = realloc(row->chars, row->size + 2);
    memmove(&row->chars[position + 1], &row->chars[position], row->size - position + 1);
    row->size++;
    row->chars[position] = chr;
}

void editorInsertChar(int chr)
{
    if (edit_conf.cy + edit_conf.row_offset == edit_conf.numrows)
    {
        editorInsertRow("", 0, edit_conf.numrows);
    }
    editorRowInsertChar(&edit_conf.rows[edit_conf.cy + edit_conf.row_offset], edit_conf.cx, chr);
    edit_conf.cx++;
}

void editorRowDelChar(editrow *row, int position)
{
    if (position < 0 || position >= row->size)
        return;
    memmove(&row->chars[position], &row->chars[position + 1], row->size - position);
    row->size--;
}

void editorRowAppendString(editrow *row, char *s, size_t len)
{
    row->chars = realloc(row->chars, row->size + len + 1);
    memcpy(&row->chars[row->size], s, len);
    row->size += len;
    row->chars[row->size] = '\0';
}

void editorDelRow(int position)
{
    if (position < 0 || position >= edit_conf.numrows)
        return;
    editrow *row = &edit_conf.rows[position];
    free(row->chars);
    memmove(&edit_conf.rows[position], &edit_conf.rows[position + 1], sizeof(editrow) * (edit_conf.numrows - position - 1));
    edit_conf.numrows--;
}

void editorDelChar()
{
    if (edit_conf.cy == edit_conf.numrows)
        return;
    if (edit_conf.cx == 0 && edit_conf.cy == 0)
        return;

    editrow *row = &edit_conf.rows[edit_conf.cy];
    if (edit_conf.cx > 0)
    {
        editorRowDelChar(row, edit_conf.cx - 1);
        edit_conf.cx--;
    }
    else
    {
        int new_x = edit_conf.rows[edit_conf.cy - 1].size;
        if (new_x > edit_conf.screen_cols - 1)
            new_x = edit_conf.screen_cols - 1;
        edit_conf.cx = new_x;
        editorRowAppendString(&edit_conf.rows[edit_conf.cy - 1], row->chars, row->size);
        editorDelRow(edit_conf.cy);
        edit_conf.cy--;
    }
}

void cBAppend(cache_buffer *cb, const char *s, int len)
{
    char *new = realloc(cb->cbuffer, cb->len + len);
    if (new == NULL)
        return;
    memcpy(&new[cb->len], s, len);
    cb->cbuffer = new;
    cb->len += len;
}
void cBFree(cache_buffer *cb)
{
    free(cb->cbuffer);
}

int getWindowSize(int *rows, int *cols)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
    {
        return -1;
    }
    else
    {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

char *parse_line(char *input, int input_len, int *len_out)
{
    int tabs = 0;
    for (int i = 0; i < input_len; i++)
    {
        if (input[i] == '\t')
        {
            tabs++;
        }
    }
    int new_len = input_len + 3 * tabs;
    char *output = malloc(new_len);
    int diff = 0;
    for (int i = 0; i < input_len; i++)
    {
        if (input[i] == '\t')
        {
            for (int j = 0; j < 4; j++)
            {
                output[i + diff] = ' ';
                diff++;
            }
            diff--;
        }
        else
        {
            output[i + diff] = input[i];
        }
    }
    *len_out = new_len;
    return output;
}

void render_editor(cache_buffer *cbuf)
{
    for (int y = 0; y < edit_conf.screen_rows; y++)
    {
        int filerow = y + edit_conf.row_offset;
        if (filerow >= edit_conf.numrows)
        {
            if (y == 2 && edit_conf.numrows == 0)
            {
                char welcome[80];
                int welcome_len = snprintf(welcome, sizeof(welcome), "RPGEditor version %s", CURRENT_VERSION);
                if (welcome_len > edit_conf.screen_cols)
                    welcome_len = edit_conf.screen_cols;

                int padding = (edit_conf.screen_cols - welcome_len) / 2;

                if (padding)
                {
                    cBAppend(cbuf, "~", 1);
                    padding--;
                }
                while (padding--)
                {
                    cBAppend(cbuf, " ", 1);
                }

                cBAppend(cbuf, welcome, welcome_len);
            }

            else
            {
                cBAppend(cbuf, "~", 1);
            }
        }
        else
        {
            int len = edit_conf.rows[filerow].size;
            if (len > edit_conf.screen_cols)
                len = edit_conf.screen_cols;
            int out_len = 0;
            char *parsed = parse_line(edit_conf.rows[filerow].chars, len, &out_len);
            cBAppend(cbuf, parsed, out_len);
        }

        cBAppend(cbuf, "\x1b[K", 3);

        cBAppend(cbuf, "\r\n", 2);
    }
    write(STDOUT_FILENO, "\x1b[H", 3);
}

void render_status_bar(cache_buffer *cbuf)
{
    cBAppend(cbuf, "\x1b[7m", 4);
    char status[40], r_status[40];

    int len = snprintf(status, sizeof(status), "%.20s - %d lines",
                       edit_conf.file_name ? edit_conf.file_name : "[No Name]", edit_conf.numrows);

    int rlen = snprintf(r_status, sizeof(r_status), "%d/%d",
                        edit_conf.cy + 1 + edit_conf.row_offset, edit_conf.numrows);

    if (len > edit_conf.screen_cols)
        len = edit_conf.screen_cols;
    cBAppend(cbuf, status, len);

    while (len < edit_conf.screen_cols)
    {
        if (edit_conf.screen_cols - len == rlen)
        {
            cBAppend(cbuf, r_status, rlen);
            break;
        }
        else
        {
            cBAppend(cbuf, " ", 1);
            len++;
        }
    }
    cBAppend(cbuf, "\x1b[m", 3);
}

void editorRefreshScreen()
{
    cache_buffer cb = CBUFFER_INIT;

    cBAppend(&cb, "\x1b[?25l", 6);
    cBAppend(&cb, "\x1b[H", 3);

    render_editor(&cb);
    render_status_bar(&cb);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", edit_conf.cy + 1, edit_conf.cx + 1);
    cBAppend(&cb, buf, strlen(buf));

    cBAppend(&cb, "\x1b[?25h", 6);

    write(STDOUT_FILENO, cb.cbuffer, cb.len);
    cBFree(&cb);
}

void die(const char *s)
{
    editorRefreshScreen();
    perror(s);
    exit(1);
}

void editorOpen(char *file_name)
{
    free(edit_conf.file_name);
    edit_conf.file_name = strdup(file_name);

    FILE *fp = fopen(file_name, "r");
    if (!fp)
        die("fopen");

    char *line;
    size_t linecap = 0;
    ssize_t linelen;

    while ((linelen = getline(&line, &linecap, fp)) != -1)
    {

        while (linelen > 0 && (strcmp(&line[linelen - 1], "\n") == 0 || strcmp(&line[linelen - 1], "\r") == 0))
        {
            linelen--;
        }
        editorInsertRow(line, linelen, edit_conf.numrows);
    }
    free(line);
    fclose(fp);
}

char *editorRowsToString(int *buflen)
{
    int total_len = 0;
    for (int j = 0; j < edit_conf.numrows; j++)
    {
        total_len += edit_conf.rows[j].size + 1;
    }
    *buflen = total_len;
    char *buf = malloc(total_len);
    char *buf_iter = buf;
    for (int j = 0; j < edit_conf.numrows; j++)
    {
        memcpy(buf_iter, edit_conf.rows[j].chars, edit_conf.rows[j].size);
        buf_iter += edit_conf.rows[j].size;
        *buf_iter = '\n';
        buf_iter++;
    }
    return buf;
}

void editorSave()
{
    if (edit_conf.file_name == NULL)
        return;
    int len;
    char *buf = editorRowsToString(&len);
    int fd = open(edit_conf.file_name, O_RDWR | O_CREAT, 0644);
    if (fd == -1)
    {
        close(fd);
        free(buf);
        return;
    }
    if (ftruncate(fd, len) != -1)
    {
        if (write(fd, buf, len) == len)
        {
            close(fd);
            free(buf);
            return;
        }
        close(fd);
    }
    free(buf);
}

void disableRawMode()
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &edit_conf.original_term_mode) == -1)
    {
        die("tcsetattr");
    }
}

void enableRawMode()
{
    if (tcgetattr(STDIN_FILENO, &edit_conf.original_term_mode) == -1)
    {
        die("tcgetattr");
    }
    struct termios raw = edit_conf.original_term_mode;

    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_oflag &= ~(OPOST);
    raw.c_iflag &= ~(IXON | ICRNL);

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    {
        die("tcsetattr");
    }

    atexit(disableRawMode);
}

int editorReadKey()
{
    int read_code;
    char character;
    while ((read_code = read(STDIN_FILENO, &character, 1)) != 1)
    {
        if (read_code == -1 && errno != EAGAIN)
            die("read");
    }

    if (character == '\x1b')
    {
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1)
            return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1)
            return '\x1b';
        if (seq[0] == '[')
        {
            if (seq[1] >= '0' && seq[1] <= '9')
            {
                if (read(STDIN_FILENO, &seq[2], 1) != 1)
                    return '\x1b';
                if (seq[2] == '~')
                {
                    switch (seq[1])
                    {
                    case '5':
                        return PAGE_UP;
                    case '6':
                        return PAGE_DOWN;
                    case '3':
                        return DEL_KEY;
                    }
                }
            }
            else
            {
                switch (seq[1])
                {
                case 'A':
                    return ARROW_UP;
                case 'B':
                    return ARROW_DOWN;
                case 'C':
                    return ARROW_RIGHT;
                case 'D':
                    return ARROW_LEFT;
                case '3':
                    return DEL_KEY;
                }
            }
        }
        return '\x1b';
    }
    return character;
}

void editorDisplayKeypress(char character)
{
    if (iscntrl(character))
    {
        printf("%d\n", character);
    }
    else
    {
        printf("%d ('%c')\r\n", character, character);
    }
}

void snap_to_line_end()
{
    if (edit_conf.cx > edit_conf.rows[edit_conf.cy + edit_conf.row_offset].size)
    {
        edit_conf.cx = edit_conf.rows[edit_conf.cy + edit_conf.row_offset].size;
    }
}

void editorProcessKeypress()
{
    int character_code = editorReadKey();
    // editorDisplayKeypress(character_code);

    switch (character_code)
    {
    case CTRL_KEY('q'):
        editorRefreshScreen();
        exit(0);
        break;

    case CTRL_KEY('s'):
        editorSave();
        break;

    case ARROW_UP:
    case 'w':
        if (edit_conf.cy != 0)
        {
            edit_conf.cy--;
        }
        else
        {
            if (edit_conf.row_offset > 0)
            {
                edit_conf.row_offset--;
            }
        }
        snap_to_line_end();
        break;

    case ARROW_LEFT:
    case 'a':
        if (edit_conf.cx != 0)
        {
            edit_conf.cx--;
        }
        snap_to_line_end();
        break;

    case ARROW_DOWN:
    case 's':
        if (edit_conf.cy != edit_conf.screen_rows - 1)
        {
            edit_conf.cy++;
        }
        else
        {
            if (edit_conf.row_offset < edit_conf.numrows - edit_conf.screen_rows)
            {
                edit_conf.row_offset++;
            }
        }
        snap_to_line_end();
        break;

    case ARROW_RIGHT:
    case 'd':
        if (edit_conf.cx != edit_conf.screen_cols - 1)
        {
            edit_conf.cx++;
        }
        snap_to_line_end();
        break;

    case PAGE_UP:
        edit_conf.cy = 0;
        snap_to_line_end();
        break;

    case PAGE_DOWN:
        edit_conf.cy = edit_conf.screen_rows - 1;
        snap_to_line_end();
        break;

    case '\r':
        editorInsertNewline();
        break;

    case BACKSPACE:
    case CTRL_KEY('h'):
        editorDelChar();
        break;

    case CTRL_KEY('l'):
    case '\x1b':
        // DO NOT DO ANYTHING
        break;

    default:
        editorInsertChar(character_code);
        break;
    }
}

/*** init ***/

int main(int argc, char *argv[])
{
    enableRawMode();
    if (getWindowSize(&edit_conf.screen_rows, &edit_conf.screen_cols) == -1)
        die("getWindowSize");
    edit_conf.cx = 0;
    edit_conf.cy = 0;
    edit_conf.row_offset = 0;
    edit_conf.numrows = 0;
    edit_conf.rows = NULL;
    edit_conf.screen_rows--;
    edit_conf.file_name = NULL;
    if (argc >= 2)
    {
        editorOpen(argv[1]);
    }

    editorRefreshScreen();
    while (1)
    {
        editorProcessKeypress();
        editorRefreshScreen();
    }
    return 0;
}