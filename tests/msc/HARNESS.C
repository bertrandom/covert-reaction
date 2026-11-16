/* Harness: covert-decomp's generate_case and helpers (CASE.C, CLUE.C, PERSON.C,
 * NEWCHAR.C, GFX.C), copied with the drawing removed, compiled with MSC 5.1 to
 * compare against covert-reaction's port. */
#include <stdio.h>

extern char *strcpy();
extern char *strcat();
extern int open(), read(), close();
extern int rand();
extern srand();
extern char *memset();

struct Person { int org; int id; int city; unsigned known; unsigned flags; unsigned roles; int points; int fate; };
struct Site { int org; int city; unsigned flags; int strength; int f8, fa; };
struct Clue { char org; char city; int suspect; int kind; int text; int pad[2]; };
struct City { char name[12]; char country[12]; int code; int size; unsigned orgs_known; unsigned orgs_visited; unsigned char map_x; unsigned char map_y; };
struct Plot { char suspect; char f1; int state; int msg; char name[32]; int flags; int items; int severity; };
struct Suspect { int person; int importance; char name[32]; unsigned char info; char f25; unsigned flags; unsigned found; int type; int rank; int f2e; };
struct SuspectState { int time; int progress; int f4; };
struct Org { char abbrev[6]; char name[20]; int home; int strength; int range; unsigned flags; char nationality; char f23; };
struct Item { char name[16]; char icon; char holder; };

/* persons[-1] in FINAL: the bytes "env.sve\0env.sve\0" */
struct Person pers_all[65] = { { 0x6e65, 0x2e76, 0x7673, 0x0065, 0x6e65, 0x2e76, 0x7673, 0x0065 } };
#define persons (pers_all + 1)
struct Site sites[256];
struct Clue clues[80];
struct City cities[16];
struct Plot plots[48];
struct Suspect suspects[32];
struct SuspectState suspect_state[32];
struct Org orgs[16];
struct Item items[4];
int city_clue_count[16], org_clue_count[16];
int num_persons, num_sites, num_plots, num_suspects, difficulty, num_msgs;
unsigned mm_known; int mm_org, mm_city, mm_id, cur_month;
int here_city, here_org, game_time, msg_day, env_var_52, env_var_38;
int episode_crime, world_num, org1, org2, org3, city1, city2, city3;
unsigned double_agents; unsigned options;
int briefing_org, face_check, lineup_leader, lineup_right, lineup_faces[15];

#define suspect_id(n) (*(int *)&suspects[n].info)

int face_defs[26][8] = {
    { 0, 1, 2, 2, 2, 2, 3, 1 }, { 0, 0, 3, 3, 3, 3, 1, 2 }, { 0, 1, 5, 3, 4, 6, 0, 3 }, { 0, 0, 8, 4, 6, 1, 3, 1 },
    { 0, 1, 5, 5, 5, 5, 0, 0 }, { 0, 1, 6, 6, 6, 6, 3, 2 }, { 0, 1, 4, 8, 5, 2, 3, 1 }, { 0, 1, 7, 7, 8, 7, 1, 3 },
    { 0, 1, 1, 2, 3, 7, 2, 1 }, { 0, 1, 3, 1, 8, 5, 3, 3 }, { 0, 1, 2, 2, 2, 3, 3, 0 }, { 0, 0, 3, 3, 3, 1, 2, 1 },
    { 0, 1, 4, 8, 5, 3, 3, 2 }, { 0, 0, 8, 4, 6, 5, 3, 3 }, { 0, 1, 7, 7, 8, 8, 0, 0 }, { 0, 1, 6, 6, 6, 8, 3, 3 },
    { 1, 0, 2, 2, 2, 2, 3, 2 }, { 1, 0, 4, 4, 4, 4, 1, 3 }, { 1, 0, 7, 1, 8, 5, 0, 3 }, { 1, 0, 8, 5, 3, 7, 3, 3 },
    { 1, 1, 1, 8, 4, 2, 1, 2 }, { 1, 1, 4, 7, 2, 1, 0, 2 }, { 1, 1, 5, 2, 6, 3, 0, 0 }, { 1, 1, 5, 5, 5, 5, 3, 0 },
    { 1, 0, 5, 3, 1, 6, 3, 1 }, { 1, 0, 2, 6, 7, 8, 0, 0 }
};

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

iabs(x)
int x;
{
    return x < 0 ? -x : x;
}

face_code_for(p)
int p;
{
    int i, code;

    code = face_defs[p][1] * 2 + face_defs[p][6] * 4 - face_defs[p][0] + 1;
    i = 0;
    do
        code += face_defs[p][i + 2] - 1 << i * 3 + 4;
    while (++i < 4);
    return code;
}

city_distance(a, b)
unsigned a, b;
{
    int dx;
    int dy;

    dx = iabs((a & 15) - (b & 15));
    dy = iabs((((unsigned char)a & 0xF0) >> 4) - (((unsigned char)b & 0xF0) >> 4));
    if (dx > dy)
        return dx * 2 + dy;
    return dy * 2 + dx;
}

org_alert_level(city, org)
int city, org;
{
    int d;
    int t;
    int level;

    d = city_distance(cities[city].code, orgs[org].home);
    level = (cities[city].size + 4) * orgs[org].strength / (d + 1);
    if (level > 0) {
        t = level * 60 / orgs[org].range + 10;
        level = level * 6 / orgs[org].range + 1;
        if (level > 5)
            level = 5;
    }
    if (city == mm_city && org == mm_org)
        level = 1;
    if (level <= 0 && difficulty == 0 && city == 1 && persons[suspects[0].person].org == org)
        level = 1;
    return level;
}

org_can_reach(org, org2, city)
int org, org2, city;
{
    int d;

    d = city_distance(orgs[org].home, cities[city].code)
        + city_distance(orgs[org].home, orgs[org2].home) * 3 / 2;
    return org_alert_level(city, org2) * 4 + 16 > d;
}

add_location(org, city, flags)
int org, city;
unsigned flags;
{
    int i;

    for (i = 0; i < num_persons; i++) {
        if (persons[i].org == org && persons[i].city == city) {
            persons[i].flags |= flags;
            return i;
        }
    }
    do
        i = random(64);
    while (persons[i].id || !i);
    if (org == mm_org && city == mm_city)
        i = 0;
    if (flags) {
        persons[i].org = org;
        persons[i].city = city;
        persons[i].known = i ? 0 : mm_known;
        persons[i].flags = flags;
        persons[i].roles = 0;
        persons[i].points = 0;
        if (i) {
            persons[i].id = random(0x7ffe) + 1;
            if (!random(3))
                persons[i].id |= 1;
        } else
            persons[i].id = mm_id;
        persons[i].fate = 0;
        return i;
    }
    return -1;
}

set_location_known(org, city, flags)
int org, city;
unsigned flags;
{
    int i;

    if (flags)
        cities[city].orgs_known |= 1 << org;
    for (i = 0; i < num_sites; i++) {
        if (sites[i].org == org && sites[i].city == city) {
            if (sites[i].flags |= flags)
                return i;
            return -1;
        }
    }
    if (!flags)
        return -1;
    sites[num_sites].org = org;
    sites[num_sites].city = city;
    sites[num_sites].flags = flags;
    sites[num_sites].strength = 0;
    sites[num_sites].f8 = sites[num_sites].fa = 0;
    num_sites++;
    return num_sites - 1;
}

char crime_file[] = "e:crime0.dta ";
char world_file[] = "e:world0.dta";

load_crime_file(n)
int n;
{
    int fd;

    strcpy(crime_file, "e:crime0.dta");
    crime_file[7] = n + '0';
    if (n > 9) {
        strcpy(crime_file + 7, "10.dta");
        crime_file[8] = n % 10 + '0';
    }
    fd = open(crime_file, 0x8000);
    read(fd, &num_suspects, 2);
    read(fd, &num_plots, 2);
    read(fd, suspects, num_suspects * sizeof(struct Suspect));
    read(fd, plots, num_plots * sizeof(struct Plot));
    read(fd, items, 72);
    close(fd);
}

load_world_file(n)
int n;
{
    int nc, f, norgs;

    world_file[7] = n + '0';
    nc = 16;
    norgs = 16;
    f = open(world_file, 0x8000);
    read(f, &nc, 2);
    read(f, &norgs, 2);
    read(f, cities, nc * sizeof(struct City));
    read(f, orgs, norgs * sizeof(struct Org));
    close(f);
}

identify_leader_faces()
{
    int used[26], where, right, leader2, face;

    where = 0;
    do
        used[where] = 0;
    while (++where < 26);
    leader2 = random(26);
    right = random(15);
    used[leader2] = 1;
    for (where = 0; where < 15; where++) {
        if (where == right)
            face = leader2;
        else {
            do
                face = random(26);
            while (used[face]);
            used[face] = 1;
        }
        lineup_faces[where] = face;
    }
    lineup_leader = leader2;
    lineup_right = right;
    face_check = 1;
    return 1;
}

case_briefing(region)
int region;
{
    int org;

    if (difficulty < 2) {
        org = persons[suspects[random(num_suspects)].person].org;
        briefing_org = org;
        orgs[org].flags |= 1;
    }
    if ((cur_month == 0 || !(options & 0x80)) && !identify_leader_faces()) {
    } else {
        if (!face_check)
            options |= 0x80;
    }
}

reset_people_marks()
{
    int i, spare;

    for (i = 0; i < num_plots; i++)
        plots[i].state = 999;
}

place_participant(n)
int n;
{
    int site, tries, org, dd, city;

    tries = 0;
    do {
        suspect_id(n) = random(0x7fff);
        suspects[n].found = 0;
        suspect_state[n].time = -1;
        suspect_state[n].progress = 0;
        for (;;) {
            org = -1;
            if (suspects[n].flags & 1)
                org = org1;
            if (suspects[n].flags & 0x80)
                org = org2;
            if (suspects[n].flags & 0x40)
                org = org3;
            if (org == -1) {
                do {
                    org = random(15) + 1;
                    dd = city_distance(orgs[org].home, orgs[org2].home);
                } while ((dd > 12 || (orgs[org].home & 0xf) <= 4 || org == 1) && ++tries < 999);
            }
            city = -1;
            if (suspects[n].flags & 0x20)
                city = city1;
            if (suspects[n].flags & 0x10)
                city = city2;
            if (suspects[n].flags & 2)
                city = city3;
            if (city == -1) {
                do {
                    city = random(16);
                    dd = city_distance(cities[city].code, cities[city1].code);
                } while ((dd < 8 || !org_alert_level(city, org)) && ++tries < 999);
            }
            if (!org_alert_level(city, org) && ++tries < 999)
                continue;
            if (suspects[n].flags & 0x100) {
                org = mm_org;
                city = mm_city;
            } else if (org == mm_org && city == mm_city && ++tries < 999)
                continue;
            if (!difficulty && !n)
                city = 1;
            site = add_location(org, city, 0);
            if (site != -1 && persons[site].flags && ++tries < 999)
                continue;
            break;
        }
        if (tries < 999) {
            site = add_location(org, city, (unsigned)1 << n);
            if ((suspects[n].flags & 0x100) && persons[site].known)
                persons[site].roles |= (unsigned)1 << n;
            if (persons[site].fate < 0)
                persons[site].flags &= ~(1 << n);
        }
    } while (persons[site].fate < 0 && ++tries < 999);
    suspects[n].person = site;
    if (suspects[n].flags & 0x200)
        persons[site].id &= ~1;
    return tries < 999;
}

generate_case()
{
    int attempts, dist, nmsg, cnt, dist2, world, first, org, i, city, j, ok,
        dummy, second, rch, al, closest;

    here_city = 1;
    here_org = 1;
    env_var_52 = 0;
    msg_day = episode_crime;
    world = world_num;
    load_crime_file(msg_day);
    load_world_file(world);
    for (i = 0; i < 80; i++)
        clues[i].suspect = -1;
    for (i = 0; i < 16; i++)
        city_clue_count[i] = org_clue_count[i] = 2;
    double_agents = 0;
    for (i = 0; i < difficulty; i++)
        double_agents |= 1 << random(16);
    double_agents &= ~2;
    for (;;) {
        for (i = 0; i < 64; i++) {
            persons[i].org = -1;
            persons[i].id = 0;
            persons[i].known = 0;
            persons[i].fate = 0;
        }
        num_persons = 64;
        game_time = 99;
        env_var_38 = -1;
        attempts = 0;
        nmsg = -1;
        while (plots[++nmsg].suspect != -1)
            ;
        do {
            org1 = random(15) + 1;
            dist = city_distance(orgs[1].home, orgs[org1].home);
        } while ((dist > 10 || org1 == 1) && ++attempts < 999);
        do {
            org2 = random(15) + 1;
            dist = city_distance(orgs[1].home, orgs[org2].home);
            dist2 = city_distance(orgs[mm_org].home, orgs[org2].home);
        } while ((dist < 8 || dist2 > 8 || (orgs[org2].home & 0xf) <= 4) && ++attempts < 999);
        do {
            org3 = random(15) + 1;
            dist = city_distance(orgs[org2].home, orgs[org3].home);
        } while ((dist > 8 || (orgs[org3].home & 0xf) <= 4) && ++attempts < 999);
        do {
            city1 = random(16);
            dist = city_distance(orgs[1].home, cities[city1].code);
        } while ((dist < 8 || !org_alert_level(city1, org2) || mm_city == city1) && ++attempts < 999);
        do {
            city2 = random(16);
            dist = city_distance(cities[city1].code, cities[city2].code);
        } while ((dist > 12 || !org_alert_level(city2, org3) || mm_city == city2) && ++attempts < 999);
        do {
            city3 = random(16);
            dist = city_distance(orgs[1].home, cities[city3].code);
        } while ((dist > 10 || !org_alert_level(city3, org1)) && ++attempts < 999);
        if (attempts >= 999)
            continue;
        for (j = 0; j < 16; j++) {
            if ((orgs[j].home & 0xf) >= 8) {
                i = clamp(random(5) + (orgs[j].home & 0xf) - 2, 0, 15);
                cnt = clamp(random(5) + orgs[j].home / 16 - 2, 0, 15);
                orgs[j].home = (cnt << 4) + i;
            }
        }
        for (i = 0; i < num_suspects; i++)
            attempts += place_participant(i) ? 0 : 999;
        if (attempts < 999)
            break;
    }
    reset_people_marks();
    case_briefing(world);
    for (j = 0; j < 16; j++)
        orgs[j].flags = 0;
    for (j = 0; j < 16; j++) {
        closest = 999;
        first = second = -1;
        for (i = 0; i < 16; i++) {
            rch = org_can_reach(1, j, i);
            if ((al = org_alert_level(i, j)) != 0) {
                al = city_distance(cities[i].code, orgs[j].home);
                if (al < closest) {
                    closest = al;
                    second = first;
                    first = i;
                }
            }
            if (rch || !j) {
                set_location_known(j, i, 4);
                cities[i].orgs_known |= 1 << j;
                orgs[j].flags |= 1;
                closest = -1;
            }
        }
        if (first != -1) {
            set_location_known(j, first, 4);
            cities[first].orgs_known |= 1 << j;
        }
        if (second != -1) {
            set_location_known(j, second, 4);
            cities[second].orgs_known |= 1 << j;
        }
    }
    for (i = 0; i < 16; i++) {
        cnt = 0;
        closest = 999;
        first = second = -1;
        for (j = 4; j < 16; j++) {
            rch = org_can_reach(1, j, i);
            if ((al = org_alert_level(i, j)) != 0) {
                al = city_distance(cities[i].code, orgs[j].home);
                if (al < closest) {
                    closest = al;
                    second = first;
                    first = j;
                }
            }
            if (rch)
                cnt++;
        }
        if (cnt < 2 && first != -1) {
            set_location_known(first, i, 4);
            cities[i].orgs_known |= 1 << first;
        }
        if (cnt < 3 && second != -1) {
            set_location_known(second, i, 4);
            cities[i].orgs_known |= 1 << second;
        }
    }
    for (i = num_suspects; i < difficulty * 4 + num_suspects; i++) {
        do {
            do
                org = random(12) + 4;
            while (org == mm_org);
            do
                city = random(16);
            while (!org_alert_level(city, org));
        } while (add_location(org, city, 0) != -1);
        suspects[i].person = add_location(org, city, 0x8000);
        strcpy(suspects[i].name, "Red Herring");
        suspects[i].importance = random(4) + 3;
        do {
            suspect_id(i) = random(0x7fff);
            ok = 1;
            for (j = 0; j < i; j++)
                if ((suspect_id(i) & 0x7e) == (suspect_id(j) & 0x7e))
                    ok = 0;
        } while (!ok);
        suspects[i].flags = 0;
        suspects[i].found = 0;
        suspect_state[i].time = random(num_plots / 2) + 4;
        suspect_state[i].progress = 0;
        suspects[i].type = random(7);
        suspects[i].rank = random(7);
    }
    num_suspects += difficulty * 4;
}

dump()
{
    int i;

    printf("hdr %d %d %d %d %d %d %d %d %d %d %d %d %d %04X %d\n", here_city, here_org, msg_day,
           num_persons, game_time, env_var_38, env_var_52, org2, org3, city1, city2, num_suspects,
           num_sites, double_agents, options);
    printf("res %d %d %d %d", org1, city3, briefing_org, face_check);
    if (face_check) {
        printf(" %d %d", lineup_leader, lineup_right);
        for (i = 0; i < 15; i++)
            printf(" %d", lineup_faces[i]);
    }
    printf("\n");
    for (i = 0; i < num_suspects; i++)
        printf("s%d %d %d %04X %04X %04X %d %d %d %d\n", i, suspects[i].person, suspects[i].importance,
               suspect_id(i), suspects[i].flags, suspects[i].found, suspects[i].type, suspects[i].rank,
               suspect_state[i].time, suspect_state[i].progress);
    for (i = 0; i < 64; i++)
        if (persons[i].id)
            printf("p%d %d %04X %d %04X %04X %04X %d %d\n", i, persons[i].org, persons[i].id, persons[i].city,
                   persons[i].known, persons[i].flags, persons[i].roles, persons[i].points, persons[i].fate);
    for (i = 0; i < num_sites; i++)
        printf("t%d %d %d %04X\n", i, sites[i].org, sites[i].city, sites[i].flags);
    for (i = 0; i < 16; i++)
        printf("c%d %04X o %02X %04X\n", i, cities[i].orgs_known, orgs[i].home, orgs[i].flags);
    for (i = 0; i < num_plots; i++)
        printf("%d ", plots[i].state);
    printf("\nrng %d\n", rand());
}

int seeds[] = { 1, 77, 31337, 65535 };

main()
{
    int c, w, d, s, k, month;
    unsigned seed;

    for (month = 0; month < 2; month++)
    for (c = 0; c < 13; c++)
    for (w = 0; w < 3; w++)
    for (d = 0; d < 4; d++)
    for (s = 0; s < 4; s++) {
        if (month && s > 1)
            continue;
        seed = seeds[s];
        memset(persons, 0, 64 * sizeof(struct Person));
        memset(sites, 0, sizeof(sites));
        memset(clues, 0, sizeof(clues));
        memset(cities, 0, sizeof(cities));
        memset(orgs, 0, sizeof(orgs));
        memset(suspects, 0, sizeof(suspects));
        memset(suspect_state, 0, sizeof(suspect_state));
        memset(plots, 0, sizeof(plots));
        num_sites = num_msgs = game_time = 0;
        briefing_org = -1;
        face_check = 0;
        options = month ? 0x80 : 0;
        cur_month = month;
        difficulty = d;
        episode_crime = c;
        world_num = w;
        load_world_file(w);
        mm_org = 4 + seed % 12;
        for (k = 0; k < 12 && orgs[mm_org].flags == 0xFFFF; k++)
            mm_org = 4 + (mm_org - 4 + 1) % 12;
        mm_city = seed % 16;
        mm_id = (orgs[mm_org].flags & 0xFF) < 26 ? face_code_for(orgs[mm_org].flags & 0xFF) : 0;
        mm_known = 0;
        srand(seed);
        printf("== %d %d %d %u %d\n", c, w, d, seed, month);
        generate_case();
        dump();
    }
    return 0;
}
