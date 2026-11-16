/* Harness: covert-decomp's BUG build_board (BOARD.C) and board_status's signal
 * propagation, copied with the drawing removed, compiled with MSC 5.1 to compare
 * the board generation against covert-redux's port (redux-tool bug-gen).
 * The trace bits read from board.pic start at 0 here (they take no random
 * numbers); everything build_board writes and the next rand() are dumped. */
#include <stdio.h>

extern int rand();
extern srand();

int dir_dx[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
int dir_dy[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };
int logic[12][4][2] = {
    { { 0, 0 }, { 1, 0 }, { 1, 0 }, { 1, 0 } },
    { { 0, 0 }, { 0, 1 }, { 0, 1 }, { 0, 1 } },
    { { 0, 0 }, { 1, 1 }, { 0, 0 }, { 1, 1 } },
    { { 0, 0 }, { 0, 0 }, { 1, 1 }, { 1, 1 } },
    { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } },
    { { 0, 0 }, { 0, 1 }, { 1, 0 }, { 1, 1 } },
    { { 1, 0 }, { 0, 0 }, { 1, 1 }, { 0, 1 } },
    { { 0, 1 }, { 1, 1 }, { 0, 0 }, { 1, 0 } },
    { { 1, 1 }, { 0, 1 }, { 1, 0 }, { 0, 0 } },
    { { 0, 1 }, { 0, 0 }, { 1, 1 }, { 1, 0 } },
    { { 1, 0 }, { 1, 1 }, { 0, 0 }, { 0, 1 } },
    { { 1, 1 }, { 1, 0 }, { 0, 1 }, { 0, 0 } },
};
int traces[40][25];
int chips[7][5];
int spare_chip;
int sig[15][10];
int links[6][10];
int difficulty;
int countx_out;

random(n)
int n;
{
    return (int)((long)rand() * n >> 15);
}

clamp(v, lo, hi)
int v, lo, hi;
{
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return v;
}

board_status()
{
    int row, bx, i2, fx;

    for (i2 = 0; i2 < 7; i2++)
        for (fx = 0; fx < 5; fx++) {
            row = i2 * 2;
            bx = fx * 2;
            sig[row + 1][bx] = logic[chips[i2][fx] & 0x3F][sig[row][bx + 1] * 2 + sig[row][bx]][0];
            if (i2 < 6)
                sig[row + 2][links[i2][bx]] = sig[row + 1][bx];
            sig[row + 1][bx + 1] = logic[chips[i2][fx] & 0x3F][sig[row][bx + 1] * 2 + sig[row][bx]][1];
            if (i2 < 6)
                sig[row + 2][links[i2][bx + 1]] = sig[row + 1][bx + 1];
        }
}

build_board()
{
    int countx, px, py, i1, sumv, pair, n_, rowv, pairs;

    for (i1 = 0; i1 < 40; i1++)
        for (rowv = 0; rowv < 25; rowv++)
            traces[i1][rowv] = 0;
    do {
        countx = 0;
        for (i1 = 0; i1 < 6; i1++) {
            pair = 0;
            for (rowv = 0; rowv < 5; rowv++) {
                px = i1 * 4 + 7;
                py = rowv * 4 + 1;
                switch (random(3)) {
                case 0:
                case 1:
                    traces[px][py] = 0x44;
                    traces[px + 1][py] = 0x44;
                    traces[px][py + 1] = 0x44;
                    traces[px + 1][py + 1] = 0x44;
                    links[i1][rowv * 2] = rowv * 2;
                    links[i1][rowv * 2 + 1] = rowv * 2 + 1;
                    if (pair) {
                        countx++;
                        traces[px][py] = 0x41;
                        traces[px + 1][py] = 5;
                        traces[px][py - 1] = 0x12;
                        traces[px + 1][py - 1] = 0x90;
                        traces[px][py - 2] = 9;
                        traces[px + 1][py - 2] = 0x21;
                        traces[px][py - 3] = 0x50;
                        traces[px + 1][py - 3] = 0x14;
                        links[i1][rowv * 2] = rowv * 2 - 1;
                        links[i1][rowv * 2 - 1] = rowv * 2;
                    }
                    pair = 1;
                    break;
                case 2:
                    traces[px][py] = 0x48;
                    traces[px + 1][py] = 0x24;
                    traces[px][py + 1] = 0x42;
                    traces[px + 1][py + 1] = 0x84;
                    links[i1][rowv * 2] = rowv * 2 + 1;
                    links[i1][rowv * 2 + 1] = rowv * 2;
                    pair = 0;
                    break;
                }
            }
        }
    } while (countx < difficulty * 4 || countx > difficulty * 4 + 4);
    countx_out = countx;
    for (i1 = 0; i1 < 10; i1++) {
        rowv = 0;
        do {
            sig[rowv][i1] = -1;
            rowv++;
        } while (rowv < 14);
        sig[0][i1] = (!(i1 & 3) || (i1 & 3) == 3) ? 1 : 0;
    }
    i1 = 1;
    do {
        px = i1 * 4 + 7;
        py = 17;
        traces[px][py + 1] |= 0x10;
        traces[px + 1][py + 1] |= 0x10;
        i1++;
    } while (i1 < 6);
    do {
        for (i1 = 0; i1 < 7; i1++)
            for (rowv = 0; rowv < 5; rowv++)
                do
                    n_ = chips[i1][rowv] = clamp(random(difficulty + 6) + difficulty, 0, 11);
                while (i1 == 6 && n_ < 4);
        board_status();
        sumv = 0;
        pairs = 0;
        i1 = 0;
        do {
            sumv += sig[13][i1];
            sig[14][i1] = !sig[13][i1];
            if (sig[13][i1 & 0xE] && sig[13][(i1 & 0xE) + 1])
                pairs++;
            i1++;
        } while (i1 < 10);
    } while (sumv != 6 - difficulty / 2 || pairs && pairs / 2 > 4 - difficulty);
    for (i1 = 0; i1 < 7; i1++)
        for (rowv = 0; rowv < 5; rowv++)
            if (random(3) + 1 >= difficulty || i1 == 6)
                chips[i1][rowv] |= 0x80;
    n_ = 0;
    while (n_ < difficulty * 2) {
        px = random(6);
        py = random(5);
        if (chips[px][py] > 3 && !(chips[px][py] & 0x40) && (px != 5 || difficulty == 4)) {
            chips[px][py] |= 0x40;
            if (difficulty != 4)
                chips[px][py] |= 0x80;
            n_++;
        }
    }
    spare_chip = clamp(random(difficulty + 6) + difficulty, 0, 11);
    for (i1 = 0; i1 < 10; i1++) {
        rowv = 0;
        do {
            sig[rowv][i1] = -1;
            rowv++;
        } while (rowv < 14);
        sig[0][i1] = (!(i1 & 3) || (i1 & 3) == 3) ? 1 : 0;
    }
    board_status();
}

main(argc, argv)
int argc;
char **argv;
{
    int d, s, x, y, i;
    unsigned long h;

    for (d = 0; d < 4; d++)
        for (s = 0; s < 250; s++) {
            difficulty = d;
            srand(s);
            build_board();
            printf("d%d s%d c%d sp%d ", d, s, countx_out, spare_chip);
            for (x = 0; x < 7; x++)
                for (y = 0; y < 5; y++)
                    printf("%02x", chips[x][y]);
            printf(" ");
            for (x = 0; x < 6; x++)
                for (y = 0; y < 10; y++)
                    printf("%x", links[x][y]);
            printf(" ");
            for (i = 0; i < 10; i++)
                printf("%d", sig[13][i]);
            h = 0;
            for (x = 0; x < 40; x++)
                for (y = 0; y < 25; y++)
                    h = h * 31 + (unsigned)traces[x][y];
            printf(" h%08lx r%d\n", h, rand());
        }
    return 0;
}
