/* Minimal GUI text grid with SDL3 + SDL3_ttf: draws a file, types characters,
   redraws only on events (SDL_WaitEvent, no timer).  Prints timings to stderr. */
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1e3+t.tv_nsec/1e6;}
#define ROWS 60
static char line[ROWS][256];
static TTF_Text *txt[ROWS];
static int inject(void *u) { (void)u; SDL_Delay(500);
  for (int i = 0; i < 200; i++) { SDL_Event e = {0}; e.type = SDL_EVENT_TEXT_INPUT; e.text.text = "x"; SDL_PushEvent(&e); SDL_Delay(10); }
  SDL_Event q = {0}; q.type = SDL_EVENT_USER; SDL_PushEvent(&q); return 0; }
int main(int argc, char **argv) {
  double t0 = now();
  FILE *f = fopen(argv[1], "r"); for (int i = 0; f && i < ROWS && fgets(line[i], 256, f); i++) line[i][strcspn(line[i], "\n")] = 0;
  if (f) fclose(f);
  if (!SDL_Init(SDL_INIT_VIDEO) || !TTF_Init()) { fprintf(stderr, "%s\n", SDL_GetError()); return 1; }
  SDL_Window *w; SDL_Renderer *r;
  if (!SDL_CreateWindowAndRenderer("ed", 1600, 1000, SDL_WINDOW_RESIZABLE, &w, &r)) { fprintf(stderr, "window: %s\n", SDL_GetError()); return 1; }
  TTF_Font *font = TTF_OpenFont(argv[2], 15);
  TTF_TextEngine *eng = TTF_CreateRendererTextEngine(r);
  int ch = TTF_GetFontLineSkip(font);
  for (int i = 0; i < ROWS; i++) txt[i] = TTF_CreateText(eng, font, line[i], 0);
  SDL_StartTextInput(w);
  if (SDL_getenv("INJECT")) SDL_CreateThread(inject, "inj", 0);
  int frames = 0; double tdraw = 0, first = 0;
  for (int quit = 0, dirty = 1; !quit;) {
    if (dirty) {
      double a = now();
      SDL_SetRenderDrawColor(r, 30, 30, 30, 255); SDL_RenderClear(r);
      for (int i = 0; i < ROWS; i++) TTF_DrawRendererText(txt[i], 4, 4 + i * ch);
      SDL_RenderPresent(r);
      double b = now(); if (!frames) { first = b - t0; fprintf(stderr, "first frame %.1f ms after start\n", first); } else tdraw += b - a;
      frames++; dirty = 0;
    }
    SDL_Event e; if (!SDL_WaitEvent(&e)) break;
    switch (e.type) {
      case SDL_EVENT_QUIT: quit = 1; break;
      case SDL_EVENT_TEXT_INPUT: { size_t n = strlen(line[0]); if (n + strlen(e.text.text) < 255) { strcat(line[0], e.text.text); TTF_SetTextString(txt[0], line[0], 0); dirty = 1; } break; }
      case SDL_EVENT_USER: quit = 1; break;
      case SDL_EVENT_WINDOW_EXPOSED: case SDL_EVENT_WINDOW_RESIZED: dirty = 1; break;
    }
  }
  if (frames > 1) fprintf(stderr, "redraws %d, mean %.2f ms\n", frames - 1, tdraw / (frames - 1));
  TTF_Quit(); SDL_Quit(); return 0;
}
