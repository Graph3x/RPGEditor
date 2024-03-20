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

/*** data ***/

struct editorConfig
{
    struct termios original_term_mode;
    int screen_rows;
    int screen_cols;
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
        cBAppend(cbuf, "~", 1);

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
    cBAppend(&cb, "\x1b[2J", 4);
    cBAppend(&cb, "\x1b[H", 3);
    editorDrawRows(&cb);
    cBAppend(&cb, "\x1b[H", 3);
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

char editorReadKey()
{
    int read_code;
    char character;
    while ((read_code = read(STDIN_FILENO, &character, 1)) != 1)
    {
        if (read_code == -1 && errno != EAGAIN)
            die("read");
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
    char character = editorReadKey();
    editorDisplayKeypress(character);

    switch (character)
    {
    case CTRL_KEY('q'):
        editorRefreshScreen();
        exit(0);
        break;
    }
}

/*** init ***/

int main()
{
    enableRawMode();
    if (getWindowSize(&edit_conf.screen_rows, &edit_conf.screen_cols) == -1)
        die("getWindowSize");
    editorRefreshScreen();

    while (1)
    {
        editorProcessKeypress();
    }
    return 0;
}