/*** includes ***/
#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <string.h>

/*** defines ***/
#define CTRL_KEY(k) ((k) & 0x1f)
#define CURRENT_VERSION "0.0.1"

/*** data ***/

struct editorConfig
{
    struct termios original_term_mode;
    int screen_rows;
    int screen_cols;

    int cx;
    int cy;
};

struct editorConfig edit_conf;

struct cache_buffer
{
    char *cbuffer;
    int len;
};

#define CBUFFER_INIT \
    {                \
        NULL, 0      \
    }

enum editorKey
{
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    PAGE_UP,
    PAGE_DOWN,
    DEL_KEY,
};

/*** functions ***/

void cBAppend(struct cache_buffer *cb, const char *s, int len)
{
    char *new = realloc(cb->cbuffer, cb->len + len);
    if (new == NULL)
        return;
    memcpy(&new[cb->len], s, len);
    cb->cbuffer = new;
    cb->len += len;
}
void cBFree(struct cache_buffer *cb)
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

void editorDrawRows(struct cache_buffer *cbuf)
{
    for (int y = 0; y < edit_conf.screen_rows; y++)
    {

        if (y == 2)
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

        cBAppend(cbuf, "\x1b[K", 3);
        if (y < edit_conf.screen_rows - 1)
        {
            cBAppend(cbuf, "\r\n", 2);
        }
    }
    write(STDOUT_FILENO, "\x1b[H", 3);
}

void editorRefreshScreen()
{
    struct cache_buffer cb = CBUFFER_INIT;

    cBAppend(&cb, "\x1b[?25l", 6);
    cBAppend(&cb, "\x1b[H", 3);

    editorDrawRows(&cb);

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
    case ARROW_UP:
    case 'w':
        if (edit_conf.cy != 0)
        {
            edit_conf.cy--;
        }
        break;
    case ARROW_LEFT:
    case 'a':
        if (edit_conf.cx != 0)
        {
            edit_conf.cx--;
        }
        break;
    case ARROW_DOWN:
    case 's':
        if (edit_conf.cy != edit_conf.screen_rows - 1)
        {
            edit_conf.cy++;
        }
        break;
    case ARROW_RIGHT:
    case 'd':
        if (edit_conf.cx != edit_conf.screen_cols - 1)
        {
            edit_conf.cx++;
        }
        break;
    case PAGE_UP:
        edit_conf.cy = 0;
        break;
    case PAGE_DOWN:
        edit_conf.cy = edit_conf.screen_rows;
        break;
    }
}

/*** init ***/

int main()
{
    enableRawMode();
    if (getWindowSize(&edit_conf.screen_rows, &edit_conf.screen_cols) == -1)
        die("getWindowSize");
    edit_conf.cx = 0;
    edit_conf.cy = 0;
    editorRefreshScreen();
    while (1)
    {
        editorProcessKeypress();
        editorRefreshScreen();
    }
    return 0;
}