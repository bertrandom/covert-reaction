/* Harness: covert-decomp's CHASE generate_city_map (CITY.C), the car offers of
 * car_select_screen, init_cars, chase_loop, car_in_view and road_open (DRIVE.C),
 * copied with the drawing, sound and keyboard removed, compiled with MSC 5.1 to
 * compare against covert-redux's port (redux-tool chase-gen).
 * For every city x difficulty x mode (0 follow, 2 chased) it dumps the map, the
 * buildings, the cars every 250 frames of a chase nobody steers, the end and
 * the next rand(). */
#include <stdio.h>

extern int rand();
extern srand();

typedef struct Car {
    int x, y, dir, follow, tx, ty, req, heat, speed, bits;
} Car;

int dir_dx[4] = { 0, 1, 0, -1 };
int dir_dy[4] = { -1, 0, 1, 0 };
unsigned speed_masks[5] = { 0, 1, 9, 21, 27 };

int city_id, target_building, options, difficulty, alert_level, is_night, is_cga;
int chase_mode, real_mission, cur_car, chase_clock, chase_over, outcome, score;
int heat_limit, in_range, show_targets, escape_timer, lost_count, turn_toggle;
int car_ahead, target_x, target_y, scene;
unsigned map_seed;
unsigned char city_map[24][24];
int bld_x[16], bld_y[16];
int chase_start, chase_dest;
Car cars[3];

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

sign(n)
int n;
{
    if (n > 0) return 1;
    if (n < 0) return -1;
    return 0;
}

road_open(x, y, dir)
int x, y, dir;
{
    if (x == 0 && dir != 1) return 0;
    if (y == 0 && dir != 2) return 0;
    if (x == 23 && dir != 3) return 0;
    if (y == 23 && dir != 0) return 0;
    switch (dir) {
    case 0:
        return city_map[x][y - 1] & 8;
    case 1:
        return city_map[x][y] & 1;
    case 2:
        return city_map[x][y] & 8;
    case 3:
        return city_map[x - 1][y] & 1;
    }
    return 0;
}

roll_offers(specs)
int *specs;
{
    int c, total, i, j;

    for (i = 0; i < 4; i++) {
        do {
            c = random(64);
            total = c & 3;
            if (c & 4) total += 2;
            if (c & 8) total++;
            total -= (c & 0x30) >> 4;
            for (j = 0; j < i; j++)
                if (specs[j] == c) total = -1;
        } while (total != 3 - difficulty || (c & 3) == 3);
        specs[i] = c;
    }
}

generate_city_map()
{
    int lx, dist, nk, byn, i, ok, j, m, sidej;

    if (real_mission)
        srand((unsigned)city_id * 123 + (map_seed & 0x3fff));
    for (i = 0; i < 24; i++)
        for (j = 0; j < 24; j++)
            city_map[i][j] = random(16) * 16 + 15;
    nk = 0;
    do {
        i = random(21) + 2;
        j = random(21) + 2;
        sidej = nk & 3;
        if (nk & 2) {
            if ((city_map[i][j] & 15) != 15) continue;
            if ((city_map[i + dir_dx[sidej]][j + dir_dy[sidej]] & 15) != 15) continue;
        }
        city_map[i][j] &= 255 - (1 << sidej);
        city_map[i][j] = ((i + j & 1) << 4) + (city_map[i][j] & 15) + 128;
        city_map[i + dir_dx[sidej]][j + dir_dy[sidej]] &= 255 - (1 << (nk + 2 & 3));
    } while (++nk < 192);
    for (nk = 0; nk < 16; nk++) {
        do {
            do {
                if (difficulty < 2) {
                    lx = random(12) * 32 + 192;
                    byn = random(12) * 32 + 208;
                } else {
                    lx = random(18) * 32 + 96;
                    byn = random(18) * 32 + 112;
                }
            } while (!road_open(lx / 32, byn / 32, 2));
            ok = 1;
            for (m = 1; m < nk; m++) {
                dist = abs(lx - bld_x[m]) + abs(byn - bld_y[m]);
                if (dist < difficulty * 32 + 64) {
                    ok = 0;
                    break;
                }
            }
        } while (!ok);
        bld_x[nk] = lx;
        bld_y[nk] = byn;
    }
    chase_start = random(15) + 1;
    do
        chase_dest = random(15) + 1;
    while (chase_start == chase_dest);
    if (real_mission) {
        chase_start = 0;            /* (char)ENV[9] */
        if (options & 0x20) {
            do
                chase_start = random(16);
            while (chase_start == target_building);
        }
        chase_dest = target_building;
    }
}

init_cars()
{
    int i;

    heat_limit = 1000 / (difficulty + 3);
    heat_limit = heat_limit * 10 / (alert_level + 100) * 10;
    outcome = 0;
    cars[0].x = bld_x[chase_start];
    cars[0].y = bld_y[chase_start];
    cars[0].dir = (bld_y[chase_dest] < cars[0].y) ? 0 : 2;
    cars[0].tx = cars[0].x;
    cars[0].ty = cars[0].y;
    cars[0].req = -1;
    cars[0].heat = 0;
    cars[0].bits = 0;
    cars[0].speed = 2;
    for (i = 1; i < 3; i++) {
        cars[i].x = cars[0].x - (dir_dx[cars[0].dir] << 5) / 4 * i;
        cars[i].y = cars[0].y - (dir_dy[cars[0].dir] << 5) / 4 * i;
        cars[i].dir = cars[0].dir;
        cars[i].follow = 1;
        cars[i].tx = -1;
        cars[i].req = -1;
        cars[i].heat = 0;
        cars[i].speed = 0;
    }
    target_x = bld_x[chase_dest];
    target_y = bld_y[chase_dest];
    if (chase_mode == 2) cur_car = 0;
}

car_in_view(n)
int n;
{
    int open;
    int ij;
    int bkx, row;
    int rx, distx, dy, ady;
    int dir;

    rx = cars[0].x - cars[n].x;
    dy = cars[0].y - cars[n].y;
    distx = abs(rx);
    ady = abs(dy);
    if (is_night && (distx > 64 || ady > 64))
        return 0;
    open = 1;
    bkx = (cars[n].x + (cars[n].dir == 3 ? 31 : 0)) / 32;
    row = (cars[n].y + (cars[n].dir == 0 ? 31 : 0)) / 32;
    if (distx == 0 && ady < 192 && cars[n].x % 32 == 0) {
        dir = dy > 0 ? 2 : 0;
        for (ij = 0; ij <= ady / 32; ij++) {
            if (!road_open(bkx, row, dir)) {
                open = 0;
                break;
            }
            row += sign(dy);
        }
        car_ahead = dir == 0 ? dy < 0 : dy > 0;
        if (!open)
            return 0;
        return ady;
    }
    if (ady != 0 || distx >= 192 || cars[n].y % 32 != 0)
        return 0;
    dir = rx > 0 ? 1 : 3;
    for (ij = 0; ij <= distx / 32; ij++) {
        if (!road_open(bkx, row, dir)) {
            open = 0;
            break;
        }
        bkx += sign(rx);
    }
    car_ahead = dir == 3 ? rx < 0 : rx > 0;
    if (!open)
        return 0;
    return distx;
}

chase_loop()
{
    int inview, attempts, lateral, i;
    unsigned newdir;
    int cx, moved, k, cy, dx, leader, dy, ty;

    if (chase_mode != 2 && chase_clock == 32) {
        cars[1].speed = cars[2].speed = 2;
    }
    in_range = 0;
    for (i = 0; i < 3; i++) {
        leader = (chase_mode != 2) ? (i == 0) : (i != 0);
        moved = 0;
        if (speed_masks[cars[i].speed] & (1 << (chase_clock % 6))) {
            cars[i].x += dir_dx[cars[i].dir];
            cars[i].y += dir_dy[cars[i].dir];
            moved = 1;
            if (!leader && cars[i].x == cars[i].tx && cars[i].y == cars[i].ty) cars[i].tx = -1;
            if (i == cur_car) scene = ++scene & 7;
        }
        if (leader) {
            score = cars[i].heat / heat_limit;
            if (cars[i].follow || show_targets) {
                cars[i].tx = cars[i].x;
                cars[i].ty = cars[i].y;
                cars[i].req = cars[i].dir;
            }
            cars[i].follow = 0;
            if (escape_timer && --escape_timer == 0) chase_over = 1;
            if (chase_mode != 2 && cars[i].x == target_x && cars[i].y == target_y && !escape_timer) {
                cars[i].speed = 0;
                escape_timer = -(difficulty * 64 - 256);
            }
        }
        inview = car_in_view(i);
        if (i != 0 && inview != 0) {
            if (chase_mode != 2) {
                if (cars[i].speed) {
                    cars[i].heat++;
                    if (inview <= 32) cars[i].heat++;
                    if (cars[i].speed > 2) cars[i].heat++;
                    cars[i].heat += ((cars[i].bits & 0x30) >> 4) - 1;
                }
                cars[0].follow = 1;
                if (cars[i].heat > heat_limit * 2) {
                    cars[0].heat++;
                    if (heat_limit / 2 < cars[0].heat && !escape_timer) cars[0].speed = 3;
                    if (cars[0].heat > heat_limit && !escape_timer) cars[0].speed = 4;
                }
                if (escape_timer && chase_mode != 2) {
                    outcome = 1;
                    if (++lost_count > 16) chase_over = 1;
                }
            }
            if (chase_mode != 2 && !escape_timer && cars[0].speed < cars[i].speed)
                cars[0].speed = cars[i].speed;
            if (((cars[0].dir + 2) & 3) == cars[i].dir) {
                if (abs(cars[i].x - cars[0].x) + abs(cars[i].y - cars[0].y) <= 8) {
                    in_range = 1;
                    if (chase_mode == 2) outcome = chase_over = 1;
                }
            }
            if (chase_mode == 2) {
                cars[i].tx = cars[i].x;
                cars[i].ty = cars[i].y;
                cars[i].follow = 1;
                cars[i].speed = clamp((chase_clock >> 3) - i, 0, inview < 32 ? 2 : 3);
            }
        } else {
            if (i != 0 && cars[i].heat > 0) cars[i].heat--;
            if (i != 0 && chase_mode == 2) {
                cars[i].speed = 3;
                if (difficulty == 0) cars[i].speed = (chase_clock & (16 << i)) ? 2 : 3;
                if (difficulty == 3) cars[i].speed = (chase_clock & (16 << i)) ? 3 : 4;
                inview = abs(cars[0].x - cars[i].x) + abs(cars[0].y - cars[i].y);
                if ((5 - difficulty) * 32 / 2 >= inview) {
                    cars[i].tx = cars[i].x;
                    cars[i].ty = cars[i].y;
                    cars[i].follow = 1;
                }
            }
        }
        if (moved && cars[i].x % 32 == 0 && cars[i].y % 32 == 0) {
            cx = cars[i].x / 32;
            cy = cars[i].y / 32;
            if (i == 0) {
                if (chase_mode == 2) {
                    if (cars[i].req != -1 && road_open(cx, cy, cars[i].req)) {
                        cars[i].dir = cars[i].req;
                        cars[i].req = -1;
                    }
                    if (!road_open(cx, cy, cars[i].dir)) {
                        attempts = 0;
                        do {
                            do newdir = random(4);
                            while (!road_open(cx, cy, newdir));
                        } while (((cars[i].dir + 2) & 3) == newdir && ++attempts < 9);
                        cars[i].dir = newdir;
                    }
                } else {
                    if (!escape_timer) cars[0].speed = 2;
                    dx = target_x - cars[i].x;
                    dy = target_y - cars[i].y;
                    if ((random(2) || dy == 0) && dx != 0) {
                        newdir = dx > 0 ? 1 : 3;
                        if (!road_open(cx, cy, newdir)) newdir = dy > 0 ? 2 : 0;
                    } else {
                        newdir = dy > 0 ? 2 : 0;
                        if (!road_open(cx, cy, newdir)) newdir = dx > 0 ? 1 : 3;
                    }
                    if (cars[i].heat > random(heat_limit * 4)) newdir = random(4);
                    attempts = 0;
                    while (!road_open(cx, cy, newdir) ||
                           (((cars[i].dir + 2) & 3) == newdir && ++attempts < 9))
                        newdir = random(4);
                    cars[i].dir = newdir;
                    for (k = 1; k < 3; k++) {
                        if (car_in_view(k)) {
                            cars[1].tx = dir_dx[cars[0].dir] * 2 + cars[0].x;
                            cars[1].ty = dir_dy[cars[0].dir] * 2 + cars[0].y;
                        }
                    }
                }
            } else {
                if (chase_mode == 2) {
                    dx = cars[0].x - cars[i].x;
                    dy = cars[0].y - cars[i].y;
                    switch (cars[0].dir) {
                    case 0: ty = -dy; lateral = dx; break;
                    case 2: ty = dy; lateral = -dx; break;
                    case 1: ty = dx; lateral = dy; break;
                    case 3: ty = -dx; lateral = -dy; break;
                    }
                    if (lateral == 0) {
                        newdir = ((turn_toggle++ & 1) * 2 + cars[0].dir - 1) & 3;
                        if (((cars[i].dir + 2) & 3) == newdir || !road_open(cx, cy, newdir))
                            newdir = cars[0].dir;
                        if (!road_open(dir_dx[newdir] + cx, dir_dy[newdir] + cy, cars[0].dir))
                            newdir = cars[0].dir;
                        if (ty < 0) newdir = (cars[0].dir + 2) & 3;
                        if (ty > 32) newdir = cars[0].dir;
                    } else if (-abs(random(lateral)) < ty) {
                        newdir = cars[0].dir;
                        if (((cars[i].dir + 2) & 3) == newdir || !road_open(cx, cy, newdir))
                            newdir = (lateral > 0 ? cars[0].dir + 1 : cars[0].dir - 1) & 3;
                    } else {
                        newdir = (lateral > 0 ? cars[0].dir + 1 : cars[0].dir - 1) & 3;
                        if (((cars[i].dir + 2) & 3) == newdir || !road_open(cx, cy, newdir))
                            newdir = cars[0].dir;
                    }
                } else {
                    newdir = cars[i].dir;
                    if (cars[i].req != -1) {
                        if (road_open(cx, cy, cars[i].req)) {
                            newdir = cars[i].req;
                            cars[i].req = -1;
                        }
                    } else if (cars[i].follow == 1) {
                        dx = cars[1].tx - cars[i].x;
                        dy = cars[1].ty - cars[i].y;
                        if (cars[i].bits & 4) {
                            if (sign(dx) != sign(cars[0].x - cars[i].x) ||
                                sign(dy) != sign(cars[0].y - cars[i].y)) {
                                dx = sign(cars[0].x - cars[i].x) * 4;
                                dy = sign(cars[0].y - cars[i].y) * 4 - random(2) * 2 + 1;
                            }
                        }
                        if (dx || dy) {
                            newdir = abs(dx) > abs(dy) ? (dx > 0 ? 1 : 3) : (dy > 0 ? 2 : 0);
                            if (((cars[i].dir + 2) & 3) == newdir || !road_open(cx, cy, newdir))
                                newdir = abs(dx) <= abs(dy) ? (dx > 0 ? 1 : 3) : (dy > 0 ? 2 : 0);
                            if (((cars[i].dir + 2) & 3) == newdir) newdir = cars[i].dir;
                        }
                    }
                    if (car_in_view(i)) {
                        cars[1].tx = dir_dx[cars[0].dir] * 2 + cars[0].x;
                        cars[1].ty = dir_dy[cars[0].dir] * 2 + cars[0].y;
                    }
                }
                if (road_open(cx, cy, newdir) && ((cars[i].dir + 2) & 3) != newdir) {
                } else {
                    attempts = 0;
                    do {
                        do newdir = random(4);
                        while (!road_open(cx, cy, newdir));
                    } while (((cars[i].dir + 2) & 3) == newdir && ++attempts < 9);
                }
                cars[i].dir = newdir;
            }
        }
    }
    if (chase_mode != 2 && cars[0].follow == 0L && heat_limit * 2 < cars[0].heat && !escape_timer)
        chase_over = 2;
}

dump_cars()
{
    int i;

    printf(" t%d", chase_clock);
    for (i = 0; i < 3; i++)
        printf(" %d,%d,%d,%d,%d,%d,%d,%d", cars[i].x, cars[i].y, cars[i].dir, cars[i].speed,
               cars[i].heat, cars[i].follow, cars[i].tx, cars[i].ty);
    printf(" s%d e%d r%d\n", scene, escape_timer, in_range);
}

main()
{
    int city, d, m, i, j, specs[4];
    unsigned long h;

    for (city = 0; city < 16; city++)
    for (d = 0; d < 4; d++)
    for (m = 0; m <= 2; m += 2) {
        city_id = city;
        difficulty = d;
        chase_mode = m;
        is_night = d & 1;
        alert_level = city * 7;
        target_building = 4 + city % 12;
        options = 0x20;
        real_mission = 1;
        map_seed = 0;
        cur_car = 1;
        chase_clock = chase_over = outcome = score = 0;
        escape_timer = lost_count = turn_toggle = in_range = scene = 0;
        for (i = 0; i < 3; i++) {
            cars[i].x = cars[i].y = cars[i].dir = cars[i].follow = 0;
            cars[i].tx = cars[i].ty = cars[i].req = cars[i].heat = 0;
            cars[i].speed = cars[i].bits = 0;
        }
        srand(city * 4 + d + m * 100);
        if (m != 2) {
            roll_offers(specs);
            cars[1].bits = specs[0];
            cars[2].bits = specs[1];
        }
        generate_city_map();
        init_cars();
        h = 0;
        for (i = 0; i < 24; i++)
            for (j = 0; j < 24; j++)
                h = h * 31 + city_map[i][j];
        printf("c%d d%d m%d map%08lx st%d de%d hl%d b%d,%d", city, d, m, h, chase_start, chase_dest,
               heat_limit, cars[1].bits, cars[2].bits);
        for (i = 0; i < 16; i++)
            printf(" %d,%d", bld_x[i], bld_y[i]);
        printf("\n");
        while (chase_clock < 3000 && !chase_over) {
            chase_loop();
            chase_clock++;
            if (chase_clock % 250 == 0) dump_cars();
        }
        printf("end");
        dump_cars();
        printf("over%d out%d score%d lost%d rand%d\n", chase_over, outcome, score, lost_count, rand());
    }
    return 0;
}
