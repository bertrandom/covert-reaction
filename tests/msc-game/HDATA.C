/* HDATA.C - the GAME globals the decomp's TIME/CHRONO/CLUE/MSGTXTG/PERSON/TRAVEL
 * objects use, laid out (initialized, so MSC keeps them in _DATA in this order)
 * where the original reads past a table into its neighbour in GAME's DGROUP:
 *   persons[-1]  = 16 bytes of "One moment please...\n" (DGROUP 0084h)
 *   msgs[-1]     = the end of cities[15], a word, num_persons (7CB4h..7CC1h)
 *   msgs[128]    = face_shown, popup_flag, plots[0] (83C2h..)
 *   suspects[-1] = face_img_b.. (9776h): no person (the end of a crime's plot
 *                  list has suspect -1)
 * The port (src/game/Sim.cpp) reads the same bytes. */
#include "game.h"

char persons_m1[16] = "ment please...\n";
struct Person persons[64] = { 0 };
struct Site sites[256] = { 0 };
struct Clue clues[80] = { 0 };
struct City cities[16] = { 0 };
int gap_7cbe = 0;
int num_persons = 0;
struct Msg msgs[128] = { 0 };
int face_shown = 0;
int popup_flag = 0;
struct Plot plots[64] = { 0 };
/* suspects[-1].person: GAME has face_img_b there, a DOS segment from the graphics
 * driver (FACE.C draw_face), never a person slot; the port uses 7FFFh too. */
int suspects_m1[24] = { 0x7fff };
struct Suspect suspects[43] = { 0 };
struct SuspectState suspect_state[43] = { 0 };
struct Org orgs[16] = { 0 };
struct Agent agents[4] = { 0 };
int city_clue_count[16] = { 0 };
int org_clue_count[16] = { 0 };
