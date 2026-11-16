/* Harness: covert-decomp's TAC generate_building and its helpers (ROOM.C),
 * copied verbatim between the markers below (randomize() is a no-op here, so
 * the next rand() can be dumped), compiled with MSC 5.1 to compare the building
 * generation against covert-reaction's port (reaction-tool tac-gen).
 * wall_map sits in a struct between guards_left and cur_org, as in TAC's DGROUP,
 * because split_rooms reads a few bytes on either side of it. */
#include <stdio.h>

extern int rand();
extern srand();
extern char *strcpy();
extern char *memset();

struct Person {
    int org, id, city;
    unsigned known;
    int clues, found, points, status;
};
struct Agent {
    char name[0x10];
    char rank;
    char person;
};
struct RoomType {
    char name[0x10];
    int value;
    unsigned sizes;
    unsigned modes;
};
struct ObjType {
    char name[0xc];
    int sx, sy;
    unsigned flags;
    unsigned rooms;
};
struct Room {
    char x, y, w, h;
    long links;
    unsigned flags;
    int type;
    int dist;
};
struct Door {
    char room_a, room_b;
    char x, y;
    char dir;
};

struct {
    char pad[62];
    int guards_left;            /* CF88 */
    char wall_map[40][32];      /* CF8A */
    int cur_org;                /* D48A */
    char tail[256];
} L;
#define wall_map L.wall_map
#define guards_left L.guards_left
#define cur_org L.cur_org

int tac_mode, n_rooms, n_doors, start_room, post_room, cur_room, on_alert, target_person;
int room_type_filter = 1;
struct Room rooms[24];
struct Door doors[96];
int room_doors[24];
char building[40][32];
char room_map[40][32];
struct Person persons[64];
struct Agent agents[4];
int door_dx[5] = { 0, 1, 0, -1, 0 };
int door_dy[5] = { -1, 0, 1, 0, -1 };

struct RoomType room_types[10] = {
    { "Office", 4, 2, 7 },
    { "File Room", 6, 1, 7 },
    { "Computer Room", 8, 4, 7 },
    { "Cafeteria", 2, 4, 7 },
    { "Lounge", 2, 2, 7 },
    { "Cipher Room", 12, 1, 7 },
    { "Bed Room", 6, 2, 0 },
    { "Bathroom", 12, 1, 7 },
    { "Kitchen", 4, 2, 0 },
    { "Storage Room", 4, 4, 7 },
};
struct ObjType obj_types[62] = {
    { "Wall", 0, 0, 0x0, 0x0 },
    { "Wall", 16, 0, 0x0, 0x0 },
    { "Wall", 32, 0, 0x0, 0x0 },
    { "Wall", 48, 0, 0x0, 0x0 },
    { "Wall", 64, 0, 0x0, 0x0 },
    { "Wall", 80, 0, 0x0, 0x0 },
    { "Wall", 96, 0, 0x0, 0x0 },
    { "Wall", 112, 0, 0x0, 0x0 },
    { "Door", 64, 16, 0x12, 0x0 },
    { "Door", 80, 16, 0x12, 0x0 },
    { "Door", 96, 16, 0x12, 0x0 },
    { "Door", 112, 16, 0x12, 0x0 },
    { "Chair", 128, 0, 0x1, 0x219 },
    { "Chair", 144, 0, 0x1, 0x218 },
    { "Chair", 160, 0, 0x1, 0x219 },
    { "Chair", 176, 0, 0x1, 0x218 },
    { "Terminal", 192, 0, 0x201, 0x25 },
    { "Typewriter", 208, 0, 0x5, 0x3 },
    { "Terminal", 224, 0, 0x201, 0x21 },
    { "Typewriter", 240, 0, 0x5, 0x3 },
    { "Chair", 256, 0, 0x1, 0x445 },
    { "Plant", 272, 0, 0x5, 0x451 },
    { "Chair", 288, 0, 0x1, 0x445 },
    { "Plant", 304, 0, 0x5, 0x451 },
    { "Files", 128, 16, 0x12b, 0x207 },
    { "Files", 144, 16, 0x12b, 0x227 },
    { "Files", 160, 16, 0x12b, 0x207 },
    { "Files", 176, 16, 0x12b, 0x227 },
    { "Floor Safe", 192, 16, 0x12b, 0x66 },
    { "Wall Safe", 208, 16, 0x10a, 0x23 },
    { "Picture", 224, 16, 0x106, 0x459 },
    { "Wall Safe", 240, 16, 0x10a, 0x463 },
    { "Table", 64, 48, 0x81, 0x318 },
    { "Table", 80, 48, 0x81, 0x318 },
    { "Desk", 32, 16, 0x4b, 0x1 },
    { "Desk", 48, 16, 0x4b, 0x1 },
    { "Desk", 0, 16, 0x4b, 0x1 },
    { "Desk", 16, 16, 0x4b, 0x1 },
    { "Couch", 96, 48, 0x45, 0x611 },
    { "Couch", 112, 48, 0x45, 0x611 },
    { "Toilet", 0, 64, 0x10b, 0x80 },
    { "Toilet", 16, 64, 0x10b, 0x80 },
    { "Sink", 96, 64, 0x10b, 0x80 },
    { "Sink", 112, 64, 0x10b, 0x80 },
    { "Bathtub", 128, 64, 0x41, 0x80 },
    { "Bathtub", 144, 64, 0x41, 0x80 },
    { "Bed", 192, 48, 0x41, 0x40 },
    { "Bed", 208, 48, 0x41, 0x40 },
    { "Range", 160, 64, 0x101, 0x100 },
    { "Range", 176, 64, 0x101, 0x100 },
    { "Refrigerator", 192, 80, 0x121, 0x100 },
    { "Refrigerator", 208, 80, 0x121, 0x100 },
    { "Stereo", 224, 48, 0x101, 0x650 },
    { "Television", 240, 48, 0x101, 0x440 },
    { "Lamp", 256, 48, 0x101, 0x608 },
    { "Television", 272, 48, 0x101, 0x400 },
    { "Computer", 0, 48, 0x125, 0x24 },
    { "Drawers", 16, 96, 0x10b, 0x440 },
    { "Computer", 32, 48, 0x125, 0x24 },
    { "Drawers", 48, 96, 0x10b, 0x440 },
    { "MacGuffin  ", 0, 96, 0x3, 0x0 },
    { "Local Agent", 256, 16, 0x3, 0x0 },
};

random(n)
int n;
{
    return (int)((long)rand() * n >> 15);
}

randomize()
{
}

/* ---- covert-decomp src/TAC/ROOM.C, generate_building .. set_wall ---- */
generate_building()
{
    int a, b, i;

    do {
        for (i = 0; i < 24; i++)
            rooms[i].flags = 0;
        n_rooms = 0;
        if (tac_mode == 3) {
            init_building_grid();
            label_rooms();
            start_room = post_room = 1;
            break;
        }
        do {
            do {
                init_building_grid();
                if (tac_mode != 3) split_rooms();
            } while (!label_rooms());
        } while (!place_doors());
    } while (n_rooms - assign_room_types() < 12);
    for (i = 1; i <= n_rooms; i++)
        place_objects(i);
    for (i = 0; i < 39; i++) {
        if ((building[i][0] & 0xfc) != 8) building[i][0] = 0xff;
        if ((building[i][31] & 0xfc) != 8) building[i][31] = 0xff;
    }
    for (i = 0; i < 31; i++) {
        if ((building[0][i] & 0xfc) != 8) building[0][i] = 0xff;
        if ((building[39][i] & 0xfc) != 8) building[39][i] = 0xff;
    }
    randomize();
}

/* Clear the 40x32 grid and wall in the building (12x13 for a bomb). */
init_building_grid()
{
    int x, y, k, w, h;

    if (tac_mode == 3) {
        w = 12;
        h = 13;
    } else {
        w = 40;
        h = 32;
    }
    for (x = 0; x < 40; x++)
        for (y = 0; y < 32; y++) {
            wall_map[x][y] = room_map[x][y] = 0;
            building[x][y] = 0xff;
        }
    for (k = 1; k < w - 1; k++) {
        set_wall(k, 1, 0);
        set_wall(k, h - 2, 2);
        wall_map[k][0] = wall_map[k][31] = 0;
    }
    for (k = 1; k < h - 1; k++) {
        set_wall(1, k, 3);
        set_wall(w - 2, k, 1);
        wall_map[0][k] = wall_map[39][k] = 0;
    }
}

/* Partition the floor with up to 20 interior walls, each started from an
 * existing wall and run straight until it meets another one. */
split_rooms()
{
    int t, cnt, w, x, i, y, ok, dir;

    for (w = 0; w < 20; w++) {
        t = 0;
        do {
            x = random(38) + 1;
            y = random(30) + 1;
            dir = -1;
            if (wall_map[x][y] == 1) dir = 2;
            if (wall_map[x][y] == 2) dir = 3;
            if (wall_map[x][y] == 4) dir = 0;
            if (wall_map[x][y] == 8) dir = 1;
            ok = 1;
            for (i = -4; i <= 4; i++)
                if (((dir & 1) ? 5 : 10) & wall_map[x + door_dx[dir + 1] * i][y + door_dy[dir + 1] * i])
                    ok = 0;
        } while ((dir == -1 || !ok) && ++t < 999);
        if (t >= 999) break;
        cnt = -1;
        do {
            if (wall_map[x][y]) cnt++;
            set_wall(x, y, (dir + 1) & 3);
            x += door_dx[dir];
            y += door_dy[dir];
        } while (!cnt && x >= 0 && y >= 0 && x < 40 && y < 40);
    }
}

/* Number the rooms and measure them; 0 if one is too big. */
label_rooms()
{
    int x, i, y, ok, len;

    n_rooms = 0;
    for (y = 1; y < 31; y++) {
        len = 1;
        for (x = 1; x < 39; x++) {
            if (!(wall_map[x][y] & 8)) {
                room_map[x][y] = room_map[x - 1][y];
                len++;
                rooms[room_map[x][y]].w = len;
            } else {
                len = 1;
                if (!(wall_map[x][y] & 1)) {
                    room_map[x][y] = room_map[x][y - 1];
                    rooms[room_map[x][y]].h++;
                } else {
                    room_map[x][y] = ++n_rooms;
                    rooms[n_rooms].x = x;
                    rooms[n_rooms].y = y;
                    rooms[n_rooms].h = 1;
                }
            }
        }
    }
    ok = 1;
    for (i = 1; i <= n_rooms; i++)
        if (rooms[i].w > 10 || rooms[i].h > 10) ok = 0;
    return ok;
}

/* Connect the rooms with doors, walking from room to room; 0 if that fails. */
place_doors()
{
    int tries, cur, k, y2, dst, cx, hidden, ok2, di, conn;

    for (k = 0; k < 24; k++) {
        room_doors[k] = 0;
        rooms[k].links = 0;
    }
    hidden = tries = n_doors = 0;
    cur = random(n_rooms) + 1;
    conn = 1;
    do {
        do {
            switch (random(4)) {
            case 0:
                cx = rooms[cur].x + rooms[cur].w / 2;
                y2 = rooms[cur].y;
                di = 0;
                break;
            case 1:
                cx = rooms[cur].x + rooms[cur].w / 2;
                y2 = rooms[cur].y + rooms[cur].h - 1;
                di = 2;
                break;
            case 2:
                y2 = rooms[cur].y + rooms[cur].h / 2;
                cx = rooms[cur].x;
                di = 3;
                break;
            case 3:
                y2 = rooms[cur].y + rooms[cur].h / 2;
                cx = rooms[cur].x + rooms[cur].w - 1;
                di = 1;
                break;
            }
            ok2 = 1;
            dst = room_map[cx + door_dx[di]][y2 + door_dy[di]];
            if (!(rooms[cur].links & (1 << dst))) {
                for (k = -1; k <= 1; k++) {
                    if (((di & 1) ? 5 : 10) & wall_map[cx + door_dx[di + 1] * k][y2 + door_dy[di + 1] * k])
                        ok2 = 0;
                    if (((di & 1) ? 5 : 10) & wall_map[cx + door_dx[di] + door_dx[di + 1] * k]
                                                      [y2 + door_dy[di] + door_dy[di + 1] * k])
                        ok2 = 0;
                }
                if (rooms[cur].links & (1 << dst)) ;    /* (sic) empty */
            }
            if (!dst) ok2 = 0;
        } while (++tries < 999 && !ok2);
        if (!(rooms[cur].links & (1 << dst)) && tries < 999) {
            wall_map[cx][y2] &= 15 - (1 << di);
            wall_map[cx + door_dx[di]][y2 + door_dy[di]] &= 15 - (1 << ((di + 2) & 3));
            room_doors[cur]++;
            room_doors[dst]++;
            tries = 0;
            if (!rooms[dst].links) conn++;
            rooms[cur].links |= 1 << dst;
            rooms[dst].links |= 1 << cur;
            building[cx][y2] = di + 8;
            building[cx + door_dx[di]][y2 + door_dy[di]] = ((di + 2) & 3) + 8;
            doors[n_doors].room_a = cur;
            doors[n_doors].room_b = dst;
            doors[n_doors].x = cx;
            doors[n_doors].y = y2;
            doors[n_doors].dir = di;
            n_doors++;
            if (hidden && (rooms[dst].flags & 2)) hidden = 0;
        }
        cur = dst;
    } while ((conn < n_rooms || hidden) && tries < 999 && n_doors < 96);
    if (!hidden && tries < 999 && n_doors < 96) return 1;
    return 0;
}

/* Pick the entry room (on the outside, best score) and the biggest inner
 * room; drop rooms without doors.  Returns the number of rooms dropped. */
assign_room_types()
{
    int nrem, big, lost, top, i, score, sz, outer;

    lost = nrem = big = 0;
    top = -999;
    for (i = 1; i <= n_rooms; i++) {
        if (!room_doors[i]) {
            remove_room(i);
            nrem++;
        } else {
            outer = 0;
            if (rooms[i].x == 1) outer++;
            if (rooms[i].y == 1) outer++;
            if (rooms[i].x + rooms[i].w == 39) outer++;
            if (rooms[i].y + rooms[i].h == 31) outer++;
            sz = rooms[i].w * rooms[i].h;
            if (outer) {
                if (room_doors[i] < 2) {
                    remove_room(i);
                    nrem++;
                    lost = 1;
                } else {
                    score = (outer - room_doors[i]) * 50 + sz;
                    if (score > top) {
                        top = score;
                        post_room = i;
                    }
                }
            } else if (sz > big) {
                big = sz;
                start_room = i;
            }
        }
    }
    if (!lost) {
        i = 0;
        do {
            i++;
            if (i != post_room && i != start_room) {
                remove_room(i);
                nrem++;
            }
        } while (!room_doors[i] || i == post_room || i == start_room);
    }
    return nrem;
}

/* Wipe a room off the map and detach its doors. */
remove_room(r)
int r;
{
    int x, y;

    rooms[r].flags |= 1;
    for (x = rooms[r].x; x < rooms[r].x + rooms[r].w; x++)
        for (y = rooms[r].y; y < rooms[r].y + rooms[r].h; y++) {
            wall_map[x][y] = room_map[x][y] = 0;
            if ((building[x][y] & 0xfc) != 8) building[x][y] = 0xff;
        }
    if (room_doors[r]) cur_room = r;
    for (x = 0; x < n_doors; x++) {
        if (doors[x].room_a == r) doors[x].room_a = 0;
        if (doors[x].room_b == r) doors[x].room_b = 0;
    }
}

/* Choose the room's type and put furniture in it; the biggest inner room
 * gets the suspect (or the captured agent) and the clue. */
place_objects(r)
int r;
{
    int t;
    unsigned f;
    int kind, x, sv, i, y, j, ok, size, spare, obj, dx, dy, face;

    if (rooms[r].flags & 1) return;
    size = rooms[r].w * rooms[r].h;
    f = 1;
    if (size > 40) f = 2;
    if (size > 72) f = 4;
    kind = -1;
    do {
        for (i = 0; i < 10; i++)
            if ((f & room_types[i].sizes) && (room_types[i].modes & room_type_filter) && random(2)) {
                kind = i;
                break;
            }
    } while (kind == -1);
    if (tac_mode == 3) kind = 0;
    rooms[r].type = kind;
    for (i = 0; i < size / 6; i++) {
        t = 0;
        do {
            x = rooms[r].x + random(rooms[r].w);
            y = rooms[r].y + random(rooms[r].h);
            obj = -1;
            if (building[x][y] == -1) {
                ok = 1;
                for (j = 0; j < 4; j++)
                    if (building[x + door_dx[j]][y + door_dy[j]] > 4) ok = 0;
                if (ok) {
                    dx = x - rooms[r].x;
                    dy = y - rooms[r].y;
                    if (dx > dy)
                        face = rooms[r].w / 2 < dx ? 1 : 3;
                    else
                        face = rooms[r].h / 2 < dy ? 2 : 0;
                    do
                        obj = random(16) * 4 + face + 12;
                    while (obj >= 61);
                    if (obj_types[obj].flags & 0x100) obj = -1;
                }
            } else if (building[x][y] < 4) {
                do
                    obj = random(16) * 4 + (building[x][y] & 3) + 12;
                while (obj >= 61);
                if (!(obj_types[obj].flags & 0x100)) obj = -1;
            }
        } while ((obj == -1 || !(obj_types[obj].rooms & (1 << kind))) && ++t < 99);
        if (t < 99) {
            building[x][y] = obj;
            if (obj_types[obj].flags & 0x40) {
                if (obj & 1)
                    building[x - 1][y] = obj - 1;
                else
                    building[x + 1][y] = obj + 1;
            }
        }
    }
    if (r == start_room && ((target_person != -1 && tac_mode != 1) || tac_mode == 2)) {
        for (i = 0; i < 4; i++)
            if (agents[i].person == target_person && target_person != -1) {
                building[rooms[r].x + rooms[r].w / 2][rooms[r].y + rooms[r].h / 2] = 60;
                obj_types[60].sx += agents[i].rank << 4;
                sv = agents[i].name[11];
                agents[i].name[11] = 0;
                strcpy(obj_types[60].name, agents[i].name);
                agents[i].name[11] = sv;
                on_alert = 1;
            }
        building[rooms[r].x + rooms[r].w / 2][rooms[r].y + rooms[r].h / 2 - 1] = 61;
        if (!(persons[target_person].id & 1)) obj_types[61].sx += 16;
    }
}

/* Building cell for each combination of wall bits (emitted after set_wall). */
char wall_cells[16] = { 0, 0, 1, 4, 2, 0, 5, 0, 3, 7, 0, 0, 6, 0, 0, 0 };

/* Put a wall on side dir of cell (x, y) and on the facing side of the
 * neighbour cell; update both cells' map shapes. */
set_wall(x, y, dir)
int x, y, dir;
{
    wall_map[x][y] |= 1 << dir;
    if (dir == 0 && y > 0) wall_map[x][y - 1] |= 4;
    if (dir == 1 && x < 39) wall_map[x + 1][y] |= 8;
    if (dir == 2 && y < 31) wall_map[x][y + 1] |= 1;
    if (dir == 3 && x > 0) wall_map[x - 1][y] |= 2;
    building[x][y] = wall_cells[wall_map[x][y]];
    building[x + door_dx[dir]][y + door_dy[dir]] = wall_cells[wall_map[x + door_dx[dir]][y + door_dy[dir]]];
}

/* ---- end of ROOM.C ---- */

run(mode, city, org, seed, diff, target, tid)
int mode, city, org, diff, target, tid;
unsigned seed;
{
    int i, x, y;
    unsigned long h1, h2, h3;

    memset(&L, 0, sizeof(L));
    memset(rooms, 0, sizeof(rooms));
    memset(doors, 0, sizeof(doors));
    memset(room_doors, 0, sizeof(room_doors));
    memset(building, 0, sizeof(building));
    memset(room_map, 0, sizeof(room_map));
    memset(persons, 0, sizeof(persons));
    memset(agents, 0, sizeof(agents));
    n_rooms = n_doors = start_room = post_room = cur_room = on_alert = 0;
    obj_types[60].sx = 0;
    strcpy(obj_types[60].name, "MacGuffin  ");
    obj_types[61].sx = 256;
    tac_mode = mode;
    cur_org = org;
    target_person = target;
    guards_left = diff * 4 + 8;
    if (mode == 2) guards_left = diff * 2 + 4;
    if (mode == 3) guards_left = diff * 2 + 2;
    if (mode == 1) guards_left = diff * 3 + 3;
    for (i = 0; i < 4; i++) {
        agents[i].person = -1;
        strcpy(agents[i].name, "Agent Smithers");
        agents[i].rank = i;
    }
    if (target >= 0) {
        persons[target].id = tid;
        if (mode == 2) agents[target & 3].person = target;
    }
    srand((unsigned)(city + 13) * (org + 7) * 23 + (seed & 0x3fff));
    generate_building();
    h1 = h2 = h3 = 0;
    for (x = 0; x < 40; x++)
        for (y = 0; y < 32; y++) {
            h1 = h1 * 31 + (unsigned char)building[x][y];
            h2 = h2 * 31 + (unsigned char)room_map[x][y];
            h3 = h3 * 31 + (unsigned char)wall_map[x][y];
        }
    printf("m%d c%d o%d s%u d%d t%d: n%d st%d po%d cr%d al%d R", mode, city, org, seed, diff, target,
           n_rooms, start_room, post_room, cur_room, on_alert);
    for (i = 0; i <= n_rooms; i++)
        printf(" %d,%d,%d,%d,%x,%d,%lx", rooms[i].x, rooms[i].y, rooms[i].w, rooms[i].h, rooms[i].flags,
               rooms[i].type, rooms[i].links);
    printf(" D");
    for (i = 0; i < n_doors; i++)
        printf(" %d,%d,%d,%d,%d", doors[i].room_a, doors[i].room_b, doors[i].x, doors[i].y, doors[i].dir);
    printf(" H %08lx %08lx %08lx o60 %d %s o61 %d r%d\n", h1, h2, h3, obj_types[60].sx, obj_types[60].name,
           obj_types[61].sx, rand());
}

main()
{
    int city, org, i;

    for (city = 0; city < 16; city++)
        for (org = 0; org < 16; org++) {
            run(0, city, org, 0, (city + org) % 5, -1, 0);
            run(0, city, org, 12345, (city * 3 + org) % 5, -1, 0);
        }
    for (i = 0; i < 16; i++) {
        run(1, i, (i * 3) % 16, 777, i % 5, -1, 0);
        run(2, i, (i * 5 + 1) % 16, 4242, i % 4, i % 6, i);
        run(3, i, (i * 7 + 2) % 16, 999, i % 4, -1, 0);
        run(0, i, (i * 11 + 3) % 16, 31337, i % 5, 1, i);
    }
    return 0;
}
