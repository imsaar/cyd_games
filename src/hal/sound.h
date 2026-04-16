#pragma once

void sound_init();
void sound_update();      // call from loop() to drive non-blocking melodies

void sound_startup();
void sound_move();
void sound_opponent_move();
void sound_win();
void sound_lose();
void sound_gameover();
