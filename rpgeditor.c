/*** includes ***/
#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>

/*** defines ***/
#define CTRL_KEY(k) ((k) & 0x1f)

/*** data ***/

struct editorConfig
{
    struct termios original_term_mode;
};

struct editorConfig EditConf;

/*** functions ***/

void editorRefreshScreen()
{
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
}

void die(const char *s)
{
    editorRefreshScreen();
    perror(s);
    exit(1);
}

void disableRawMode()
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &EditConf.original_term_mode) == -1)
    {
        die("tcsetattr");
    }
}

void enableRawMode()
{
    if (tcgetattr(STDIN_FILENO, &EditConf.original_term_mode) == -1)
    {
        die("tcgetattr");
    }
    struct termios raw = EditConf.original_term_mode;

    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_oflag &= ~(OPOST);
    raw.c_iflag &= ~(IXON | ICRNL);

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    {
        die("tcsetattr");
    }

    atexit(disableRawMode);
}

void editorDrawRows()
{
    for (int y = 0; y < 24; y++)
    {
        write(STDOUT_FILENO, "~\r\n", 3);
    }
    write(STDOUT_FILENO, "\x1b[H", 3);
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
    editorRefreshScreen();
    // draw "graphics"
    editorDrawRows();

    while (1)
    {
        editorProcessKeypress();
    }
    return 0;
}