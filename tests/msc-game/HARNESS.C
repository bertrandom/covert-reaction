/* HARNESS.C - differential test of covert-reaction's src/game/Sim against GAME.EXE's
 * world simulation: covert-decomp's own TIME.C, CHRONO.C, CLUE.C, MSGTXTG.C,
 * PERSON.C and TRAVEL.C, compiled unchanged with MSC 5.1, linked with this file
 * (the program around them: loading the starting world, the new-game sequence
 * of GAME's main and mission_briefing, the CIA scans, the dumps) and with
 * HDATA.C (the tables) and stubs for the drawing and the screens.
 *
 *   HARNESS IN.SVE SEED DAYS SCAN
 *
 * IN.SVE: the world as `reaction-tool sim ... --save-init` writes it (a saved game:
 * the env block, the player block and the tables). SEED: GAME's randomize().
 * SCAN: 0 none, 1 local scan, 2 international scan, 3 both, after each day.
 * Prints what reaction-tool sim ... --raw prints. Run from the game directory
 * (text.dta, clues.txt, fonts.cv). */
#include <stdio.h>
#include "game.h"

extern int open(), read(), close(), atoi();
extern long atol();
extern char *memcpy();

/* ---- the globals the objects need that HDATA.C doesn't define ---- */
RastPort gfx_rp = { 0, 0, 0, 319, 199, 0, 15, 5, 1 };
RastPort *g_gfx = &gfx_rp;
RastPort *back_page = &gfx_rp;
RastPort *work_page = &gfx_rp;
int screen_width = 320;
unsigned options;
unsigned rnd_seed;
int num_msgs, num_sites, num_plots, num_suspects, difficulty;
unsigned mm_known;
int mm_org, mm_city, mm_id, cur_month;
char player_name[40], player_org_name[40];
int here_city, here_org, game_time;
unsigned clock_base;
int msg_day;
unsigned seen_orgs;
int dossier_state, clue_icon, is_cga;
char video_mode;
char text_buf[2048];
char itoa_buf[32];
char msg_key[16] = "*MSG0000";
char subst_tail[512];
int jailbreak_person = -1;
int clock_minute, clock_hour, clock_day, hiding_target, mastermind;
int heat;
unsigned mole_cities;
int in_building, city_exit, last_exit, menu_flag, menu_count, menu_items[64];
int portrait_x, portrait_y, default_sel, cur_drive = -1, anim_handle = -1;
int org2, org3, city1, city2, env_var_38, rand_seed, skill_levels, env48, env50;
int briefing_done, first_visit, cia_proximity, left_building, alert_org, clear_pending;
char scene_name[16];
int faces_f[40], faces_m[40], face_part_imgs[16];

char std_colormap[16], std_colormap2[16], status_colormap[16];
char binos_colors[16], briefing_colors[16], bugcar_colors[16], building_colors[5][16], bustout_colors[16];
char capture_colors[16], crypto_colors[16], data_colors[16], default_colors[16], escape_colors[16];
char followed_colors[16], hq_colors[16], intel_colors[16], intrgate_colors[16], jail_colors[16];
char jail_colors2[16], wounded_colors[16];
char num_str[16];

char env_mem[1024];
char player_block[0x28e];
char *cur_label = "B";

/* ---- random(): MSC's rand() with the state visible ---- */
unsigned long holdrand;

random(n)
int n;
{
    holdrand = holdrand * 214013L + 2531011L;
    return (int)((long)(int)((holdrand >> 16) & 0x7fff) * n >> 15);
}

clamp(v, lo, hi)
int v, lo, hi;
{
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return v;
}

/* ---- output ---- */
put_escaped(s)
char *s;
{
    unsigned char c;

    while ((c = *s++) != 0) {
        if (c == '\n') printf("\\n");
        else if (c == '\\') printf("\\\\");
        else if (c < 0x20 || c >= 0x7f) printf("\\x%02X", c);
        else putchar(c);
    }
}

dump(label)
char *label;
{
    int i;

    printf("D %s rng=%08lX clk=%u gt=%d nm=%d ns=%d heat=%d mole=%04X jb=%d ht=%d mm=%d hc=%d ho=%d opt=%04X ",
           label, holdrand, clock_base, game_time, num_msgs, num_sites, heat, mole_cities, jailbreak_person,
           hiding_target, mastermind, here_city, here_org, options);
    printf("ch=%d cd=%d cm=%d mmk=%04X e48=%d\n", clock_hour, clock_day, clock_minute, mm_known, env48);
    for (i = 0; i < 64; i++) {
        if (!persons[i].id && !persons[i].known) continue;
        printf("P%d %d %04X %d %04X %04X %04X %d %d\n", i, persons[i].org, persons[i].id, persons[i].city,
               persons[i].known, persons[i].flags, persons[i].roles, persons[i].points, persons[i].fate);
    }
    for (i = 0; i < num_sites && i < 256; i++)
        printf("S%d %d %d %04X %d\n", i, sites[i].org, sites[i].city, sites[i].flags, sites[i].strength);
    for (i = 0; i < 80; i++) {
        if (clues[i].suspect == -1) continue;
        printf("C%d %d %d %d %04X %04X\n", i, clues[i].org, clues[i].city, clues[i].suspect, clues[i].kind,
               clues[i].text);
    }
    for (i = 0; i < num_msgs && i < 128; i++)
        printf("M%d %d %d %d %04X %d %d %d %d %d\n", i, msgs[i].type, msgs[i].time, msgs[i].msg, msgs[i].flags,
               msgs[i].level, msgs[i].sender, msgs[i].sender_city, msgs[i].receiver, msgs[i].receiver_city);
    printf("L");
    for (i = 0; i < num_plots; i++) printf(" %d", plots[i].state);
    printf("\nU");
    for (i = 0; i < num_suspects; i++)
        printf(" %04X/%04X/%d/%d", suspects[i].flags, suspects[i].found, suspect_state[i].time,
               suspect_state[i].progress);
    printf("\nY");
    for (i = 0; i < 16; i++) printf(" %04X/%04X/%04X", cities[i].orgs_known, cities[i].orgs_visited, orgs[i].flags);
    printf("\nI");
    for (i = 0; i < 4; i++) printf(" %d", agents[i].person);
    printf("\nK");
    for (i = 0; i < 16; i++) printf(" %d", city_clue_count[i]);
    for (i = 0; i < 16; i++) printf(" %d", org_clue_count[i]);
    printf("\n");
}

/* ---- the screens' stubs that matter ---- */
incoming_message(msg)
char *msg;
{
    printf("TT ");
    put_escaped(msg);
    printf("|");
    put_escaped(text_buf);
    printf("\n");
}

int menu_answer = 0;                    /* what show_menu returns */

show_menu(s, x, y)
char *s;
int x, y;
{
    printf("MN ");
    put_escaped(s);
    printf("\n");
    return menu_answer;
}

get_key()
{
    printf("WK ");
    put_escaped(text_buf);
    printf("\n");
    return 13;
}

/* GAME.C set_exit_state, as process_events calls it when the case ends. */
set_exit_state()
{
    env48 = describe_alert_level(here_org, here_city);
    env50 = difficulty;
    printf("END\n");
    dump(cur_label);
}

/* The font widths for drv_char_width: font 1 of FONTS.CV. */
unsigned char font_file[4096];
int font_data;

load_fonts()
{
    int fd;

    fd = open("fonts.cv", 0x8000);
    read(fd, font_file, sizeof font_file);
    close(fd);
    font_data = font_file[2] | font_file[3] << 8;
}

int far drv_char_width(font, ch)
int font, ch;
{
    int first, last, fixed, spacing;

    ch &= 0xff;
    first = font_file[font_data - 8];
    last = font_file[font_data - 7];
    fixed = font_file[font_data - 5];
    spacing = font_file[font_data - 3];
    if ((ch & 0x80) || ch < first || ch > last)
        return 0;
    return (fixed ? fixed : font_file[font_data - 8 - (last - first + 1) + ch - first]) + spacing;
}

/* ---- the program ---- */
#define W(o) (*(int *)(env_mem + (o)))   /* the file starts at ENV+10h */
#define PB(o) (*(int *)(player_block + (o)))

load_world(name)
char *name;
{
    int fd;

    fd = open(name, 0x8000);
    read(fd, env_mem + 0x10, 0x3f0);
    here_city = W(0x20); here_org = W(0x22); options = W(0x24); msg_day = W(0x26);
    org2 = W(0x28); org3 = W(0x2a); city1 = W(0x2c); city2 = W(0x2e); clock_base = W(0x30);
    num_persons = W(0x32); num_sites = W(0x34); game_time = W(0x36); env_var_38 = W(0x38);
    difficulty = W(0x3a); rand_seed = W(0x3c); num_suspects = W(0x3e); num_plots = W(0x40);
    jailbreak_person = W(0x42); video_mode = W(0x44); num_msgs = W(0x46); env48 = W(0x48);
    mole_cities = W(0x4a); seen_orgs = W(0x4c); skill_levels = W(0x4e); env50 = W(0x50); heat = W(0x52);
    read(fd, player_block, 0x28e);
    mm_known = PB(4); mm_org = PB(6); mm_city = PB(8); mm_id = PB(0xa); cur_month = PB(0x16);
    memcpy(player_name, player_block + 0x2a, 40);
    memcpy(player_org_name, player_block + 0x52, 40);
    read(fd, suspects, num_suspects * 48);
    read(fd, suspect_state, num_suspects * 6);
    read(fd, plots, num_plots * 44);
    read(fd, cities, 0x220);
    read(fd, orgs, 0x240);
    read(fd, persons, 0x400);
    read(fd, sites, 0xc00);
    read(fd, clues, 0x3c0);
    read(fd, msgs, 0x700);
    read(fd, agents, 0x48);
    read(fd, city_clue_count, 0x20);
    read(fd, org_clue_count, 0x20);
    close(fd);
}

/* BRIEFING.C mission_briefing, without the screens. */
briefing()
{
    int r, total;

    total = 0;
    game_time = -1;
    do {
        if (num_msgs > 5) {
            do r = random(16);
            while (mole_cities & (1 << r));
            total += describe_new_info(1, r, 99, 0, 0);
        } else
            advance_time(180);
        in_building = 0;
    } while ((4 - difficulty) * 2 > total);
    strcpy(text_buf, "The rest is up to you. ");
    if (!difficulty)
        strcat(text_buf, "I've assigned one of my best research assistants to work with you on this case. ");
    strcat(text_buf, "The President's counting on you Max, don't let us down.");
    wrap_text_buffer(0x28);
    show_menu(text_buf, 8, 0x95);
}

/* SCAN 4: one city action a day, the original routines where they are
 * functions (CITY.C, SUSPECTS.C, SCREENS.C), the screens' logic inlined where
 * they are menus. Mirrors tools/cmd_sim.cpp cityDay. */
city_day(d)
int d;
{
    int city, o, p, i;

    switch (d % 8) {
    case 0:                             /* travel_screen after the choice */
        city = d * 5 % 16;
        if (city != here_city) {
            i = fly_to_city(here_city, city) / 8 * 30 + 60;
            here_city = city;
            add_chronology_entry(4, city);
            advance_time(i);
            heat /= 3;
        }
        break;
    case 1:                             /* hotel_screen: Visit the lounge */
        text_buf[0] = 0;
        show_prose("*lounge", 0x20);
        show_menu(text_buf, 0x14, 0x50);
        for (i = 0; i < num_suspects; i++) {
            p = suspects[i].person;
            if (persons[p].city == here_city && !(suspects[i].flags & 0x100))
                reveal_random_clue(i, 0, here_city);
        }
        raise_alertness(0, here_city, 0x14);
        break;
    case 2:
        research_office();
        break;
    case 3:
        for (o = 1; o < 16; o++) {
            if (set_location_known(o, here_city, 0) != -1) {
                menu_answer = -1;       /* Esc at the watch menu */
                watch_building(o, here_city);
                menu_answer = 0;
                break;
            }
        }
        break;
    case 4:                             /* a participant whose role we know, else any */
        o = -1;
        for (p = 0; p < 64 && o == -1; p++)
            if (persons[p].id && persons[p].fate >= 0 && (persons[p].roles & 0x7fff)) o = p;
        for (p = 0; p < 64 && o == -1; p++)
            if (persons[p].id && persons[p].fate >= 0 && (persons[p].known & 8) && (persons[p].flags & 0x7fff)) o = p;
        if (o != -1) {
            ENV[13] = persons[o].roles & 0x7fff ? 1 : 0;   /* no evidence: turn him */
            interrogate_suspect(o);
        }
        break;
    case 6:                             /* building_screen: wiretap placed (BUG: score 40, 1 alarm) */
    case 7:                             /* success_briefing after a break-in (TAC) */
        for (i = 0; i < 63; i++) {      /* go to a participant's building */
            p = (d * 7 + i) % 63 + 1;
            if (persons[p].id && persons[p].fate >= 0 && (persons[p].flags & 0x7fff) && persons[p].org > 1)
                break;
        }
        if (i == 63)
            break;
        here_city = persons[p].city;
        here_org = o = persons[p].org;
        if (d % 8 == 6) {
            raise_alertness(here_org, here_city, 1 * 50);
            p = set_location_known(o, here_city, 4);
            sites[p].strength += 40;
            advance_time(60);
        } else {
            last_exit = 1;
            ENV[8] = 30;
            ENV[9] = 20;
            ENV[10] = 1;
            success_briefing(o, here_city);
            last_exit = 0;
        }
        break;
    case 5:
        if (d / 8 % 2) {                /* cia_hq_screen: Accuse Double Agent */
            if (mole_cities & 1 << here_city) {
                strcpy(text_buf, "The local agent confesses\nand is replaced.\n");
                strcat(text_buf, "You search his files\nfor additional information...\n");
                show_menu(text_buf, 0x14, 0x14);
                mole_cities -= 1 << here_city;
                for (i = 0; i < 3; i++)
                    if (describe_new_info(1, here_city, 100, 0, 0))
                        break;
            } else {
                strcpy(text_buf, "The local agent is checked\nand proved innocent.\nYou are barred from\nusing the local office.\n");
                show_menu(text_buf, 0x14, 0x14);
                clear_location(1, here_city);
            }
        } else {
            menu_answer = d / 16 % 2;   /* the prisoner exchange: 1 agree */
            captured_screen();
            menu_answer = 0;
        }
        break;
    }
}

char day_label[8];

main(argc, argv)
int argc;
char **argv;
{
    int days, scan, d, k, found;

    *(char far * far *)0x4F0L = (char far *)env_mem;
    load_fonts();
    load_world(argv[1]);
    holdrand = atol(argv[2]);
    days = atoi(argv[3]);
    scan = atoi(argv[4]);
    g_gfx->font = 1;
    /* GAME main, new game */
    jailbreak_person = -1;
    briefing();
    here_org = 1;
    options |= 0xC;
    dump("B");
    for (d = 1; d <= days; d++) {
        itoa(d, day_label, 10);
        cur_label = day_label;
        advance_time(1440);
        if (scan & 1) {
            if (!describe_new_info(1, here_city, 100, 0, 0))
                show_menu("No new information\n", 0x14, 0x29);
            advance_time(240);
        }
        if (scan & 2) {
            found = 0;
            for (k = 0; k < 4; k++) {
                advance_time(60);
                found += describe_new_info(1, random(16), 50, 0, 0);
            }
            if (!found)
                show_menu("No new information\n", 0x14, 0x29);
        }
        if (scan & 4)
            city_day(d);
        dump(day_label);
    }
    return 0;
}

/* ---- the rest: drawing and screens, nothing to do ---- */
void far gfx_line() {}
void far gfx_fill_rect() {}
void far drv_draw_text() {}
void far drv_copy_rect() {}
int far drv_kbhit() { return 0; }
int far drv_getkey() { return 13; }
void far gfx_show_page() {}
void far drv_recolor_rect() {}
void far drv_set_palette() {}
void far drv_set_color_map() {}
void far drv_draw_image() {}
void far drv_play_sound() {}
draw_line() {}
draw_text() {}
draw_text_transparent() {}
void wait_retrace() {}
void save_env() {}
require_disk() { return 1; }
draw_portrait() {}
draw_sprite_a() {}
draw_bevel_box() {}
joystick_button() { return 0; }
joystick_to_key() { return -1; }
void flush_input() {}
draw_intercept_popup() {}
void load_picture_swapped() {}
play_sound() {}
void clear_play_area() {}
clear_screen() {}
void draw_face_parts() {}
draw_rect_outline() {}
draw_teletype_paper() {}
void far drv_slot9() {}
load_city_picture() {}
load_game_menu() { return -1; }
save_game_menu() {}
play_animation() {}
set_scene_background() { return 1; }
void set_scene_var() {}
