#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
SDL_Window *window;
SDL_Renderer *renderer;
SDL_Event event;
int palettes[4][4] = {{15, 0, 16, 48}, {15, 1, 33, 49}, {15, 6, 22, 54}, {15, 9, 25, 41}};
SDL_Color nes_palette[64];
int selectedPalette;
int selectedColor;
int cur_tile = 0;
int cur_metatile = 0;
struct Tile {
    char colors[64];
    int lastPalette;
    SDL_Texture *texture;
};
typedef struct Tile Tile;
Tile tiles[512];
struct Metatile {
    int subtiles[4];
    SDL_Texture *texture;
};
typedef struct Metatile Metatile;
enum CommandType {
    POINT,
    RECTANGLE,
    DATABLOCK,
    REFERENCE,
    MOVEMENT
};
typedef enum CommandType CommandType;
struct Command {
    CommandType type;
    int offX;
    int offY;
    int sizeX;
    int sizeY;
    int argument;
    char *datablock;
};
typedef struct Command Command;
struct Pattern {
    int sizeX;
    int sizeY;
    int numcommands;
    Command* commands;
    char* cache;
};
typedef struct Pattern Pattern;
struct Sprite {
    int relx;
    int rely;
    int tile;
    int mirrorx;
    int mirrory;
    int palette;
};
typedef struct Sprite Sprite;
struct Metasprite {
    int numsprites;
    Sprite *subsprites;
};
typedef struct Metasprite Metasprite;
Metasprite metasprites[128];
Pattern patterns[256];
Pattern *selectedPattern;
Pattern mainLevel[4];
int cur_pattern;
Metatile metatiles[64];
void draw_tile(Tile *tile, int x, int y, int w, int h) {
    SDL_Rect rect = {x, y, w, h};
    SDL_RenderCopy(renderer, tile->texture, NULL, &rect);
}
void draw_special_tile(Tile *tile, int x, int y, int w, int h, int palette, int mirrorx, int mirrory) {
    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, 8, 8);
    int pixels[64];
    SDL_Color color;
    for (int i = 0; i < 64; i++) {
        if (tile->colors[i]) {
            color = nes_palette[palettes[palette][tile->colors[i]]];
        } else {
            printf("transparent\n");
            color.a = SDL_ALPHA_TRANSPARENT;
        }
        pixels[i] = *(int*)&color;
    }
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_UpdateTexture(texture, NULL, &pixels, 32);
    SDL_Rect rect = {x, y, w, h};
    SDL_RenderCopyEx(renderer, texture, NULL, &rect, 0, NULL, mirrory*SDL_FLIP_VERTICAL | mirrorx*SDL_FLIP_HORIZONTAL);
}
void update_metatile_texture(Metatile *metatile) {
    metatile->texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, 16, 16);
    SDL_SetRenderTarget(renderer, metatile->texture);
    SDL_Rect rect = {0, 0, 8, 8};
    SDL_RenderCopy(renderer, tiles[metatile->subtiles[0]].texture, NULL, &rect);
    rect.x = 8;
    SDL_RenderCopy(renderer, tiles[metatile->subtiles[1]].texture, NULL, &rect);
    rect.x = 0;
    rect.y = 8;
    SDL_RenderCopy(renderer, tiles[metatile->subtiles[2]].texture, NULL, &rect);
    rect.x = 8;
    SDL_RenderCopy(renderer, tiles[metatile->subtiles[3]].texture, NULL, &rect);
    SDL_SetRenderTarget(renderer, NULL);
}
void update_tile_texture(Tile *tile) {
    tile->texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, 8, 8);
    int pixels[64];
    SDL_Color color;
    for (int i = 0; i < 64; i++) {
        color = nes_palette[palettes[tile->lastPalette][tile->colors[i]]];
        pixels[i] = *(int*)&color;
    }
    SDL_UpdateTexture(tile->texture, NULL, &pixels, 32);
    for (int i = 0; i < 64; i++) {
        for (int j = 0; j < 4; j++) {
            if (&tiles[metatiles[i].subtiles[j]] == tile) {
                update_metatile_texture(&metatiles[i]);
                break;
            }
        }
    }
}
void draw_metatile(Metatile *metatile, int x, int y, int w, int h) {
    SDL_Rect rect = {x, y, w, h};
    SDL_RenderCopy(renderer, metatile->texture, NULL, &rect);
}
void gen_around_box(SDL_Texture **box, int w, int h, SDL_Color color, int thickness) {
    *box = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w+thickness*2, h+thickness*2);
    SDL_SetRenderTarget(renderer, *box);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetTextureBlendMode(*box, SDL_BLENDMODE_BLEND);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_Rect rect;
    rect.x = 0;
    rect.y = 0;
    rect.w = w+thickness*2;
    rect.h = h+thickness*2;
    for (int i = 0; i < thickness; i++) {
        SDL_RenderDrawRect(renderer, &rect);
        rect.x++;
        rect.y++;
        rect.w -= 2;
        rect.h -= 2;
    }
    SDL_SetRenderTarget(renderer, NULL);
}
TTF_Font *font, *font2, *font3;
SDL_Color black = {0, 0, 0, 255};
SDL_Color white = {255, 255, 255, 255};
SDL_Color lightblue = {150, 150, 255, 255};
SDL_Texture *pointIcon;
SDL_Texture *rectIcon;
SDL_Texture *datablockIcon;
SDL_Texture *referenceIcon;
SDL_Texture *movementIcon;
int clipNewer = 0;
int can_save = 0;
void draw_text(int x, int y, char* string) {
    SDL_Surface *text = TTF_RenderText_Shaded(font, string, black, white);
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, text);
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(texture, 150);
    SDL_Rect textrect;
    textrect.x = x;
    textrect.y = y;
    SDL_QueryTexture(texture, NULL, NULL, &textrect.w, &textrect.h);
    SDL_RenderCopy(renderer, texture, NULL, &textrect);
    SDL_FreeSurface(text);
    SDL_DestroyTexture(texture);
}
void draw_text2(int x, int y, char* string) {
    SDL_Surface *text = TTF_RenderText_Solid(font2, string, white);
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, text);
    SDL_Rect textrect;
    textrect.x = x;
    textrect.y = y;
    SDL_QueryTexture(texture, NULL, NULL, &textrect.w, &textrect.h);
    SDL_RenderCopy(renderer, texture, NULL, &textrect);
    SDL_FreeSurface(text);
    SDL_DestroyTexture(texture);
}
void draw_text3(int x, int y, char* string) {
    SDL_Surface *text = TTF_RenderText_Solid(font3, string, white);
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, text);
    SDL_Rect textrect;
    textrect.x = x;
    textrect.y = y;
    SDL_QueryTexture(texture, NULL, NULL, &textrect.w, &textrect.h);
    SDL_RenderCopy(renderer, texture, NULL, &textrect);
    SDL_FreeSurface(text);
    SDL_DestroyTexture(texture);
}
void onclick_curtile(int index) {
    if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT || event.type == SDL_MOUSEMOTION && event.motion.state & SDL_BUTTON_LMASK != 0) {
        tiles[cur_tile].colors[index] = selectedColor;
        tiles[cur_tile].lastPalette = selectedPalette;
        update_tile_texture(&tiles[cur_tile]);
        can_save = 1;
    } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_MIDDLE) {
        char fillfrom = tiles[cur_tile].colors[index];
        char dofill[64];
        int changed;
        for (int i = 0; i < 64; i++) {
            dofill[i] = index == i;
        }
        do {
            changed = 0;
            for (int i = 0; i < 64; i++) {
                if (!dofill[i]) continue;
                if ((i & 7) != 7 && tiles[cur_tile].colors[i+1] == fillfrom && !dofill[i+1]) {
                    dofill[i+1] = 1;
                    changed = 1;
                }
                if ((i & 7) != 0 && tiles[cur_tile].colors[i-1] == fillfrom && !dofill[i-1]) {
                    dofill[i-1] = 1;
                    changed = 1;
                }
                if ((i & 56) != 56 && tiles[cur_tile].colors[i+8] == fillfrom && !dofill[i+8]) {
                    dofill[i+8] = 1;
                    changed = 1;
                }
                if ((i & 56) != 0 && tiles[cur_tile].colors[i-8] == fillfrom && !dofill[i-8]) {
                    dofill[i-8] = 1;
                    changed = 1;
                }
            }
        } while (changed);
        for (int i = 0; i < 64; i++) {
            if (!dofill[i]) continue;
            tiles[cur_tile].colors[i] = selectedColor;
        }
        tiles[cur_tile].lastPalette = selectedPalette;
        update_tile_texture(&tiles[cur_tile]);
        can_save = 1;
    }
}
void onclick_palette(int index) {
    if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_RIGHT) {
        selectedPalette = index >> 2;
        selectedColor = index & 3;
    } else if (event.type == SDL_MOUSEWHEEL) {
        if ((event.wheel.y > 0) != (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)) {
            palettes[index >> 2][index & 3]++;
        } else {
            palettes[index >> 2][index & 3]--;
        }
        palettes[index >> 2][index & 3] &= 63;
        for (int i = 0; i < 512; i++) {
            if (tiles[i].lastPalette == index >> 2) {
                update_tile_texture(&tiles[i]);
            }
        }
    }
}
void onclick_pattern(int index) {
    if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
        cur_tile = index;
    } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_MIDDLE) {
        tiles[index].lastPalette = tiles[cur_tile].lastPalette;
        tiles[index].texture = NULL;
        update_tile_texture(&tiles[index]);
        can_save = 1;
    }
}
void onclick_metatile(int index) {
    if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
        metatiles[cur_metatile].subtiles[index] = cur_tile;
        update_metatile_texture(&metatiles[cur_metatile]);
        can_save = 1;
    }
}
void onclick_metagrid(int index) {
    if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_RIGHT) {
        cur_metatile = index;
        can_save = 1;
    }
}
int num_added;
int last_pattern_added[1024];
int pos_history;
int upcoming_pos;
void update_pattern_cache(Pattern *pattern, int commandmax) {
    if (pattern->cache != NULL) {
        free(pattern->cache);
    }
    pattern->cache = calloc(1,pattern->sizeX*pattern->sizeY);
    if (commandmax != -1) {
        num_added = 0;
    }
    int x = 0;
    int y = 0;
    Command command;
    int i;
    for (i = 0; i < (commandmax == -1 || !clipNewer ? pattern->numcommands : commandmax); i++) {
        if (i == commandmax-1) {
            pos_history = y*pattern->sizeX+x;
        } else if (i == commandmax) {
            upcoming_pos = y*pattern->sizeX+x;
        }
        command = pattern->commands[i];
        if (command.type == POINT) {
            x += command.offX;
            y += command.offY;
            last_pattern_added[num_added++] = y*pattern->sizeX+x;
            x++;
            y++;
        } else if (command.type == RECTANGLE) {
            x += command.offX;
            y += command.offY;
            int sx = x;
            for (int j = 0; j < command.sizeY; j++) {
                x = sx;
                for (int k = 0; k < command.sizeX; k++) {
                    if (y < 0 || x < 0 || x >= pattern->sizeX || y >= pattern->sizeY) continue;
                    pattern->cache[y*pattern->sizeX+x] = command.argument;
                    if (i == commandmax-1) {
                        last_pattern_added[num_added++] = y*pattern->sizeX+x;
                    }
                    x++;
                }
                y++;
            }
        } else if (command.type == DATABLOCK) {
            x += command.offX;
            y += command.offY;
            int sx = x;
            for (int j = 0; j < command.sizeY; j++) {
                x = sx;
                for (int k = 0; k < command.sizeX; k++) {
                    if (y < 0 || x < 0 || x >= pattern->sizeX || y >= pattern->sizeY) continue;
                    pattern->cache[y*pattern->sizeX+x] = command.datablock[j*command.sizeX+k];
                    if (i == commandmax-1) {
                        last_pattern_added[num_added++] = y*pattern->sizeX+x;
                    }
                    x++;
                }
                y++;
            }
        } else if (command.type == REFERENCE) {
            x += command.offX;
            y += command.offY;
            int sx = x;
            int sy = y;
            if (pattern == &patterns[command.argument]) continue;
            update_pattern_cache(&patterns[command.argument], -1);
            for (int yl = 0; yl < command.sizeY; yl++) {
                for (int xl = 0; xl < command.sizeX; xl++) {
                    x = sx + xl*patterns[command.argument].sizeX;
                    y = sy + yl*patterns[command.argument].sizeY;
                    for (int j = 0; j < patterns[command.argument].sizeY; j++) {
                        x = sx + xl*patterns[command.argument].sizeX;
                        for (int k = 0; k < patterns[command.argument].sizeX; k++) {
                            if (y < 0 || x < 0 || x >= pattern->sizeX || y >= pattern->sizeY) continue;
                            pattern->cache[y*pattern->sizeX+x] = patterns[command.argument].cache[j*patterns[command.argument].sizeX+k];
                            if (i == commandmax-1) {
                                last_pattern_added[num_added++] = y*pattern->sizeX+x;
                            }
                            x++;
                        }
                        y++;
                    }
                }
            }
        } else if (command.type == MOVEMENT) {
            x += command.offX*8;
            y += command.offY*8;
        }
    }
    if (i == commandmax) {
        upcoming_pos = y*pattern->sizeX+x;
    }
}
int scroll = 0;
int scroll2 = 0;
void draw_pattern(Pattern *pattern) {
    for (int i = 0; i < pattern->sizeY; i++) {
        for (int j = 0; j < pattern->sizeX; j++) {
            draw_metatile(&metatiles[pattern->cache[i*pattern->sizeX+j]], (selectedPattern->sizeX > 16 ? scroll : 0)+10+16*j, 10+16*i, 16, 16);
        }
    }
}
void draw_command(Command command, int index) {
    char buffer[64];
    SDL_Rect rect;
    rect.x = 320;
    rect.y = 10+40*index+scroll2;
    rect.w = 32;
    rect.h = 32;
    int tx = 360;
    sprintf(buffer, "%d: ", index);
    draw_text2(300, 10+40*index+scroll2, buffer);
    if (command.type == POINT) {
        sprintf(buffer, "(%+d,%+d) Action %d", command.offX, command.offY, command.argument);
        SDL_RenderCopy(renderer, pointIcon, NULL, &rect);
        tx += 40;
    } else if (command.type == RECTANGLE) {
        sprintf(buffer, "(%+d,%+d) %dx%d", command.offX, command.offY, command.sizeX, command.sizeY);
        SDL_RenderCopy(renderer, rectIcon, NULL, &rect);
        tx += 40;
        draw_metatile(&metatiles[command.argument], rect.x+40, rect.y, 32, 32);
    } else if (command.type == DATABLOCK) {
        sprintf(buffer, "(%+d,%+d) %dx%d", command.offX, command.offY, command.sizeX, command.sizeY);
        SDL_RenderCopy(renderer, datablockIcon, NULL, &rect);
        tx += 40;
    } else if (command.type == REFERENCE) {
        sprintf(buffer, "(%+d,%+d) Pattern %d (%dx%d) %dx%d", command.offX, command.offY, command.argument, patterns[command.argument].sizeX, patterns[command.argument].sizeY, command.sizeX, command.sizeY);
        SDL_RenderCopy(renderer, referenceIcon, NULL, &rect);
        tx += 40;
    } else if (command.type == MOVEMENT) {
        sprintf(buffer, "(%+d,%+d)", command.offX*8, command.offY*8);
        SDL_RenderCopy(renderer, movementIcon, NULL, &rect);
        tx += 40;
    }
    draw_text2(tx, 10+40*index+scroll2, buffer);
}
int num_file_bytes() {
    // byte contributors
    // 14 bytes of the pointer listing
    // 1 byte for main level command count
    // 4 bytes per pattern for its pointer and the header size
    // 3 bytes per rectangle or reference command
    // 3 bytes plus block size for data block commands
    // 1 byte per movement command
    // 2 bytes per point command
    // 5 bytes per metatile
    // 3 bytes per metasprite
    // 4 bytes per subsprite
    int bytes = 15;
    //printf("14 bytes for pointer listing\n");
    int numpatterns = 0;
    int referenced[256];
    for (int i = 0; i < 256; i++) {
        referenced[i] = 0;
    }
    int cost = 0;
    for (int i = 0; i < 1; i++) { // TODO THAT
        cost = 1;
        for (int j = 0; j < mainLevel[i].numcommands; j++) {
            if (mainLevel[i].commands[j].type == REFERENCE) {
                referenced[mainLevel[i].commands[j].argument] = 1;
            }
            switch (mainLevel[i].commands[j].type) {
                case POINT:
                    bytes += 2;
                    cost += 2;
                    break;
                case RECTANGLE:
                    bytes += 3;
                    cost += 3;
                    break;
                case DATABLOCK:
                    bytes += 3 + mainLevel[i].commands[j].sizeX * mainLevel[i].commands[j].sizeY;
                    cost += 3 + mainLevel[i].commands[j].sizeX * mainLevel[i].commands[j].sizeY;
                    break;
                case REFERENCE:
                    bytes += 3;
                    cost += 3;
                    break;
                case MOVEMENT:
                    bytes++;
                    cost++;
                    break;
            }
        }
        //printf("%d bytes for main level slice %d\n", cost, i);
    }
    int allzeros = 0;
    for (int i = 0; i < 256; i++) {
        for (int j = 0; j < patterns[i].numcommands; j++) {
            if (patterns[i].commands[j].type == REFERENCE) {
                referenced[patterns[i].commands[j].argument] = 1;
            }
        }
    }
    for (int i = 0; i < 256; i++) {
        if (!referenced[i]) continue;
        numpatterns++;
        for (int j = 0; j < patterns[i].numcommands; j++) {
            switch (patterns[i].commands[j].type) {
                case POINT:
                    bytes += 2;
                    cost += 2;
                    break;
                case RECTANGLE:
                    bytes += 3;
                    cost += 3;
                    break;
                case DATABLOCK:
                    bytes += 3 + mainLevel[i].commands[j].sizeX * mainLevel[i].commands[j].sizeY;
                    cost += 3 + mainLevel[i].commands[j].sizeX * mainLevel[i].commands[j].sizeY;
                    break;
                case REFERENCE:
                    bytes += 3;
                    cost += 3;
                    break;
                case MOVEMENT:
                    bytes++;
                    cost++;
                    break;
            }
        }
    }
    bytes += 4 * numpatterns;
    //printf("%d bytes for pattern pointers and headers\n", 4 * numpatterns);
    allzeros = 0;
    cost = 0;
    for (int i = 0; i < 64; i++) {
        if (metatiles[i].subtiles[0] == 0 && metatiles[i].subtiles[1] == 0 && metatiles[i].subtiles[2] == 0 && metatiles[i].subtiles[3] == 0) {
            if (allzeros) {
                continue;
            } else {
                allzeros = 1;
            }
        }
        bytes += 5;
        cost += 5;
    }
    //printf("%d bytes for metatiles\n", cost);
    cost = 0;
    allzeros = 0;
    for (int i = 0; i < 128; i++) {
        if (metasprites[i].numsprites == 1 && (metasprites[i].subsprites[0].tile & 0xff) == 0) {
            if (allzeros) {
                continue;
            } else {
                allzeros = 1;
            }
        }
        bytes += 3 + 4*metasprites[i].numsprites;
        cost += 3 + 4*metasprites[i].numsprites;
    }
    //printf("%d bytes for metasprites\n", cost);
    return bytes;
}
void export_pattern(Pattern pattern, FILE *file) {
    fputc(pattern.numcommands, file);
    fputc(pattern.sizeX, file);
    fputc(pattern.sizeY, file);
    for (int i = 0; i < pattern.numcommands; i++) {
        fputc(pattern.commands[i].type, file);
        switch(pattern.commands[i].type) {
            case POINT:
                fputc(pattern.commands[i].argument, file);
                fputc(pattern.commands[i].offX, file);
                fputc(pattern.commands[i].offY, file);
                break;
            case RECTANGLE:
                fputc(pattern.commands[i].argument, file);
                fputc(pattern.commands[i].offX, file);
                fputc(pattern.commands[i].offY, file);
                fputc(pattern.commands[i].sizeX, file);
                fputc(pattern.commands[i].sizeY, file);
                break;
            case DATABLOCK:
                fputc(pattern.commands[i].offX, file);
                fputc(pattern.commands[i].offY, file);
                fputc(pattern.commands[i].sizeX, file);
                fputc(pattern.commands[i].sizeY, file);
                for (int j = 0; j < pattern.commands[i].sizeX * pattern.commands[i].sizeY; j++) {
                    fputc(pattern.commands[i].datablock[j], file);
                }
                break;
            case REFERENCE:
                fputc(pattern.commands[i].argument, file);
                fputc(pattern.commands[i].offX, file);
                fputc(pattern.commands[i].offY, file);
                fputc(pattern.commands[i].sizeX, file);
                fputc(pattern.commands[i].sizeY, file);
                break;
            case MOVEMENT:
                fputc(pattern.commands[i].offX, file);
                fputc(pattern.commands[i].offY, file);
                break;
        }
    }
}
void export_files() {
    char fname[64];
    for (int i = 0; i < 512; i++) {
        sprintf(fname, "levelold/tiles/tile%d", i);
        unlink(fname);
    }
    for (int i = 0; i < 64; i++) {
        sprintf(fname, "levelold/ltiles/ltile%d", i);
        unlink(fname);
    }
    for (int i = 0; i < 128; i++) {
        sprintf(fname, "levelold/sprites/sprite%d", i);
        unlink(fname);
    }
    for (int i = 0; i < 256; i++) {
        sprintf(fname, "levelold/patterns/pattern%d", i);
        unlink(fname);
    }
    rmdir("levelold/tiles");
    rmdir("levelold/ltiles");
    rmdir("levelold/sprites");
    rmdir("levelold/patterns");
    unlink("levelold/palette");
    unlink("levelold/mainlevel");
    rmdir("levelold");
    rename("level", "levelold");
    mkdir("level", 0777);
    mkdir("level/tiles", 0777);
    mkdir("level/ltiles", 0777);
    mkdir("level/sprites", 0777);
    mkdir("level/patterns", 0777);
    FILE *file = fopen("level/palette", "w");
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            fputc(palettes[i][j], file);
        }
    }
    fclose(file);
    for (int i = 0; i < 512; i++) {
        sprintf(fname, "level/tiles/tile%d", i);
        file = fopen(fname, "w");
        fputc(tiles[i].lastPalette, file);
        for (int j = 0; j < 64; j++) {
            fputc(tiles[i].colors[j], file);
        }
        fclose(file);
    }
    for (int i = 0; i < 64; i++) {
        sprintf(fname, "level/ltiles/ltile%d", i);
        file = fopen(fname, "w");
        for (int j = 0; j < 4; j++) {
            fputc(metatiles[i].subtiles[j], file);
        }
        fclose(file);
    }
    for (int i = 0; i < 128; i++) {
        sprintf(fname, "level/sprites/sprite%d", i);
        file = fopen(fname, "w");
        fputc(metasprites[i].numsprites, file);
        for (int j = 0; j < metasprites[i].numsprites; j++) {
            fputc(metasprites[i].subsprites[j].mirrorx, file);
            fputc(metasprites[i].subsprites[j].mirrory, file);
            fputc(metasprites[i].subsprites[j].relx, file);
            fputc(metasprites[i].subsprites[j].rely, file);
            fputc(metasprites[i].subsprites[j].palette, file);
            fputc(metasprites[i].subsprites[j].tile, file);
        }
        fclose(file);
    }
    file = fopen("level/mainlevel", "w");
    for (int i = 0; i < 4; i++) {
        export_pattern(mainLevel[i], file);
    }
    fclose(file);
    for (int i = 0; i < 256; i++) {
        sprintf(fname, "level/patterns/pattern%d", i);
        file = fopen(fname, "w");
        export_pattern(patterns[i], file);
        fclose(file);
    }
}
void import_pattern(FILE *file, Pattern *pattern) {
    pattern->numcommands = fgetc(file);
    pattern->sizeX = fgetc(file);
    pattern->sizeY = fgetc(file);
    pattern->commands = calloc(pattern->numcommands, sizeof(Command));
    for (int i = 0; i < pattern->numcommands; i++) {
        pattern->commands[i].type = fgetc(file);
        switch(pattern->commands[i].type) {
            case POINT:
                pattern->commands[i].argument = fgetc(file);
                pattern->commands[i].offX = (signed char)fgetc(file);
                pattern->commands[i].offY = (signed char)fgetc(file);
                break;
            case RECTANGLE:
                pattern->commands[i].argument = fgetc(file);
                pattern->commands[i].offX = (signed char)fgetc(file);
                pattern->commands[i].offY = (signed char)fgetc(file);
                pattern->commands[i].sizeX = fgetc(file);
                pattern->commands[i].sizeY = fgetc(file);
                break;
            case DATABLOCK:
                pattern->commands[i].offX = (signed char)fgetc(file);
                pattern->commands[i].offY = (signed char)fgetc(file);
                pattern->commands[i].sizeX = fgetc(file);
                pattern->commands[i].sizeY = fgetc(file);
                pattern->commands[i].datablock = malloc(pattern->commands[i].sizeX * pattern->commands[i].sizeY);
                for (int j = 0; j < pattern->commands[i].sizeX * pattern->commands[i].sizeY; j++) {
                    pattern->commands[i].datablock[j] = fgetc(file);
                }
                break;
            case REFERENCE:
                pattern->commands[i].argument = fgetc(file);
                pattern->commands[i].offX = (signed char)fgetc(file);
                pattern->commands[i].offY = (signed char)fgetc(file);
                pattern->commands[i].sizeX = fgetc(file);
                pattern->commands[i].sizeY = fgetc(file);
                break;
            case MOVEMENT:
                pattern->commands[i].offX = (signed char)fgetc(file);
                pattern->commands[i].offY = (signed char)fgetc(file);
                break;
        }
    }

}
void import_files() {
    can_save = 1;
    FILE *file = fopen("level/palette", "r");
    for (int i = 0; i < 16; i++) {
        palettes[i / 4][i % 4] = fgetc(file);
    }
    fclose(file);
    char fname[64];
    for (int i = 0; i < 512; i++) {
        sprintf(fname, "level/tiles/tile%d", i);
        file = fopen(fname, "r");
        tiles[i].lastPalette = fgetc(file);
        for (int j = 0; j < 64; j++) {
            tiles[i].colors[j] = fgetc(file);
        }
        update_tile_texture(&tiles[i]);
        fclose(file);
    }
    for (int i = 0; i < 64; i++) {
        sprintf(fname, "level/ltiles/ltile%d", i);
        file = fopen(fname, "r");
        for (int j = 0; j < 4; j++) {
            metatiles[i].subtiles[j] = fgetc(file);
        }
        update_metatile_texture(&metatiles[i]);
        fclose(file);
    }
    for (int i = 0; i < 128; i++) {
        sprintf(fname, "level/sprites/sprite%d", i);
        file = fopen(fname, "r");
        free(metasprites[i].subsprites);
        metasprites[i].numsprites = fgetc(file);
        metasprites[i].subsprites = calloc(metasprites[i].numsprites, sizeof(Sprite));
        for (int j = 0; j < metasprites[i].numsprites; j++) {
            metasprites[i].subsprites[j].mirrorx = fgetc(file);
            metasprites[i].subsprites[j].mirrory = fgetc(file);
            metasprites[i].subsprites[j].relx = (signed char)fgetc(file);
            metasprites[i].subsprites[j].rely = (signed char)fgetc(file);
            metasprites[i].subsprites[j].palette = fgetc(file);
            metasprites[i].subsprites[j].tile = 0b100000000 | fgetc(file);
        }
        fclose(file);
    }
    file = fopen("level/mainlevel", "r");
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < mainLevel[i].numcommands; j++) {
            if (mainLevel[i].commands[j].type == DATABLOCK) {
                free(mainLevel[i].commands[j].datablock);
            }
        }
        free(mainLevel[i].commands);
        import_pattern(file, &mainLevel[i]);
    }
    fclose(file);
    for (int i = 0; i < 256; i++) {
        sprintf(fname, "level/patterns/pattern%d", i);
        file = fopen(fname, "r");
        for (int j = 0; j < patterns[i].numcommands; j++) {
            if (patterns[i].commands[j].type == DATABLOCK) {
                free(patterns[i].commands[j].datablock);
            }
        }
        free(patterns[i].commands);
        import_pattern(file, &patterns[i]);
        update_pattern_cache(&patterns[i], -1);
        fclose(file);
    }
    for (int i = 0; i < 4; i++) {
        update_pattern_cache(&mainLevel[i], -1);
    }
}
void draw_metasprite(Metasprite *metasprite, int x, int y) {
    for (int i = 0; i < metasprite->numsprites; i++) {
        draw_special_tile(&tiles[metasprite->subsprites[i].tile], x+metasprite->subsprites[i].relx*2, y+metasprite->subsprites[i].rely*2, 16, 16, metasprite->subsprites[i].palette, metasprite->subsprites[i].mirrorx, metasprite->subsprites[i].mirrory);
    }
}
SDL_Texture *aroundBox;
SDL_Texture *aroundPalBox;
SDL_Texture *aroundColBox;
SDL_Texture *aroundMetagridBox;
SDL_Texture *aroundSpriteBox;
const int CUR_TILE_X = 10;
const int CUR_TILE_Y = 10;
const int CUR_TILE_PSIZE = 16;
const int PALETTES_X = 300;
const int PALETTES_Y = 10;
const int PALETTES_PSIZE = 32;
const int PATTERNS_X = 100;
const int PATTERNS_Y = 200;
const int PATTERNS_PSIZE = 16;
const int METATILE_X = 10;
const int METATILE_Y = 500;
const int METATILE_PSIZE = 64;
const int METAGRID_X = 200;
const int METAGRID_Y = 500;
const int METAGRID_PSIZE = 32;
int full_palette_rs[64] = {82, 1, 15, 35, 54, 64, 63, 50, 31, 11, 0, 0, 0, 0, 0, 0, 160, 30, 56, 88, 117, 132, 130, 111, 81, 49, 26, 14, 16, 0, 0, 0, 254, 105, 137, 174, 206, 224, 222, 200, 166, 129, 99, 84, 86, 60, 0, 0, 254, 190, 204, 221, 234, 242, 241, 232, 217, 201, 188, 180, 181, 169, 0, 0};
int full_palette_gs[64] = {82, 26, 15, 6, 3, 4, 9, 19, 32, 42, 47, 46, 38, 0, 0, 0, 160, 74, 55, 40, 33, 35, 46, 63, 82, 99, 107, 105, 92, 0, 0, 0, 255, 158, 135, 118, 109, 112, 124, 145, 167, 186, 196, 193, 179, 60, 0, 0, 255, 214, 204, 196, 192, 193, 199, 208, 218, 226, 230, 229, 223, 169, 0, 0};
int full_palette_bs[64] = {82, 81, 101, 99, 75, 38, 4, 0, 0, 0, 0, 10, 45, 0, 0, 0, 160, 157, 188, 184, 148, 92, 36, 0, 0, 0, 5, 46, 104, 0, 0, 0, 255, 252, 255, 255, 241, 178, 112, 62, 37, 40, 70, 125, 192, 60, 0, 0, 255, 253, 255, 255, 249, 223, 194, 170, 157, 158, 174, 199, 228, 169, 0, 0};
int command_index = 0;
int yscroll = 3;
int cur_metasprite;
int cur_subsprite;
int main() {
    SDL_Init(SDL_INIT_VIDEO);
    TTF_Init();
    font = TTF_OpenFont("font.ttf",16);
    font2 = TTF_OpenFont("font.ttf",11);
    font3 = TTF_OpenFont("font.ttf",24);
    srand(time(NULL));
    window = SDL_CreateWindow("Level editor", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 800, 800, 0);
    renderer = SDL_CreateRenderer(window, -1, 0);
    for (int i = 0; i < 64; i++) {
        nes_palette[i].r = full_palette_rs[i];
        nes_palette[i].g = full_palette_gs[i];
        nes_palette[i].b = full_palette_bs[i];
        nes_palette[i].a = SDL_ALPHA_OPAQUE;
    }
    gen_around_box(&aroundBox, 16, 16, white, 1);
    gen_around_box(&aroundPalBox, 128, 32, white, 1);
    gen_around_box(&aroundColBox, 32, 32, white, 3);
    gen_around_box(&aroundMetagridBox, 32, 32, white, 1);
    gen_around_box(&aroundSpriteBox, 16, 16, white, 1);
    SDL_Rect rect;
    char mycolors;
    for (int j = 0; j < 512; j++) {
        for (int i = 0; i < 64; i++) {
            tiles[j].colors[i] = 0;
        }
        tiles[j].lastPalette = 0;
        update_tile_texture(&tiles[j]);
    }
    for (int i = 0; i < 64; i++) {
        tiles[0].colors[i] = 0;
    }
    tiles[0].lastPalette = 0;
    update_tile_texture(&tiles[0]);
    for (int j = 0; j < 64; j++) {
        for (int i = 0; i < 4; i++) {
            metatiles[j].subtiles[i] = 0;
        }
        update_metatile_texture(&metatiles[j]);
    }
    for (int i = 0; i < 4; i++) {
        metatiles[0].subtiles[i] = 0;
    }
    update_metatile_texture(&metatiles[0]);
    for (int i = 0; i < 128; i++) {
        metasprites[i].numsprites = 1;
        metasprites[i].subsprites = malloc(sizeof(Sprite));
        metasprites[i].subsprites[0].tile = 0;
        metasprites[i].subsprites[0].relx = 0;
        metasprites[i].subsprites[0].rely = 0;
        metasprites[i].subsprites[0].mirrorx = 0;
        metasprites[i].subsprites[0].mirrory = 0;
        metasprites[i].subsprites[0].palette = 0;
    }
    SDL_Color red = {255, 0, 0, 255};
    SDL_Color blue = {0, 255, 255, 255};
    pointIcon = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, 16, 16);
    SDL_SetRenderTarget(renderer, pointIcon);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
    SDL_RenderDrawLine(renderer, 8, 2, 8, 14);
    SDL_RenderDrawLine(renderer, 2, 8, 14, 8);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderDrawLine(renderer, 8, 5, 8, 11);
    SDL_RenderDrawLine(renderer, 5, 8, 11, 8);
    SDL_SetRenderDrawColor(renderer, 0, 0, 255, 255);
    SDL_RenderDrawPoint(renderer, 8, 8);
    rectIcon = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, 16, 16);
    SDL_SetRenderTarget(renderer, rectIcon);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
    rect.x = 2;
    rect.y = 2;
    rect.w = 13;
    rect.h = 13;
    SDL_RenderFillRect(renderer, &rect);
    datablockIcon = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, 16, 16);
    SDL_SetRenderTarget(renderer, datablockIcon);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    rect.w = 4;
    rect.h = 4;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            rect.x = i*4;
            rect.y = j*4;
            if ((i+j) & 1) {
                SDL_SetRenderDrawColor(renderer, 0, 0, 255, 255);
            } else {
                SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
            }
            SDL_RenderFillRect(renderer, &rect);
        }
    }
    referenceIcon = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, 16, 16);
    SDL_SetRenderTarget(renderer, referenceIcon);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
    rect.x = 1;
    rect.y = 1;
    rect.w = 14;
    rect.h = 14;
    SDL_RenderDrawRect(renderer, &rect);
    rect.x += 4;
    rect.w -= 8;
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderFillRect(renderer, &rect);
    SDL_SetRenderTarget(renderer, NULL);
    movementIcon = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, 16, 16);
    SDL_SetRenderTarget(renderer, movementIcon);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    rect.x = 5;
    rect.y = 5;
    rect.w = 5;
    rect.h = 5;
    SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
    SDL_RenderDrawRect(renderer, &rect);
    rect.w--;
    rect.h--;
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderFillRect(renderer, &rect);
    SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
    SDL_RenderDrawLine(renderer, 1, 1, 14, 14);
    SDL_SetRenderTarget(renderer, NULL);
    int infoDisplay = 0;
    int lowerMode = 0;
    int upperMode = 0;
    int newestDisplay = 0;
    for (yscroll = 0; yscroll < 4; yscroll++) {
        selectedPattern = &mainLevel[yscroll];
        selectedPattern->commands = malloc(sizeof(Command));
        selectedPattern->commands[0].offX = 0;
        selectedPattern->commands[0].offY = 0;
        selectedPattern->commands[0].sizeX = 1;
        selectedPattern->commands[0].sizeY = 1;
        selectedPattern->commands[0].argument = 0;
        selectedPattern->commands[0].type = RECTANGLE;
        selectedPattern->numcommands = 1;
        selectedPattern->sizeX = 8*16;
        selectedPattern->sizeY = 4*16;
    }
    yscroll--;
    for (int i = 0; i < 256; i++) {
        patterns[i].cache = NULL;
        patterns[i].commands = malloc(sizeof(Command));
        patterns[i].commands[0].offX = 0;
        patterns[i].commands[0].offY = 0;
        patterns[i].commands[0].sizeX = 1;
        patterns[i].commands[0].sizeY = 1;
        patterns[i].commands[0].argument = 0;
        patterns[i].commands[0].type = RECTANGLE;
        patterns[i].numcommands = 1;
        patterns[i].sizeX = 1;
        patterns[i].sizeY = 1;
    }
    while (1) {
        SDL_Delay(10);
        const char *kstate = SDL_GetKeyboardState(NULL);
        infoDisplay = kstate[SDL_SCANCODE_TAB];
        if (kstate[SDL_SCANCODE_LEFT] && upperMode) {
            scroll+=2;
        }
        if (kstate[SDL_SCANCODE_RIGHT] && upperMode) {
            scroll-=2;
        }
        if (kstate[SDL_SCANCODE_W]) {
            if (lowerMode == 1 && cur_subsprite >= 0 && metasprites[cur_metasprite].numsprites > cur_subsprite) {
                metasprites[cur_metasprite].subsprites[cur_subsprite].rely--;
            }
        }
        if (kstate[SDL_SCANCODE_S]) {
            if (lowerMode == 1 && cur_subsprite >= 0 && metasprites[cur_metasprite].numsprites > cur_subsprite) {
                metasprites[cur_metasprite].subsprites[cur_subsprite].rely++;
            }
        }
        if (kstate[SDL_SCANCODE_A]) {
            if (lowerMode == 1 && cur_subsprite >= 0 && metasprites[cur_metasprite].numsprites > cur_subsprite) {
                metasprites[cur_metasprite].subsprites[cur_subsprite].relx--;
            }
        }
        if (kstate[SDL_SCANCODE_D]) {
            if (lowerMode == 1 && cur_subsprite >= 0 && metasprites[cur_metasprite].numsprites > cur_subsprite) {
                metasprites[cur_metasprite].subsprites[cur_subsprite].relx++;
            }
        }
        while (SDL_PollEvent(&event) != 0) {
            switch (event.type) {
                case SDL_QUIT:
                    return 0;
                case SDL_KEYDOWN:
                    switch (event.key.keysym.sym) {
                        case SDLK_LEFT:
                            if (upperMode == 0 && cur_tile > 0) {
                                cur_tile--;
                            }
                            break;
                        case SDLK_RIGHT:
                            if (upperMode == 0 && cur_tile < 511) {
                                cur_tile++;
                            }
                            break;
                        case SDLK_UP:
                            if (upperMode == 0 && cur_tile >= 32) {
                                cur_tile -= 32;
                            } else if (command_index != 0) {
                                command_index--;
                                if (-scroll2 > 40*(command_index)) {
                                    scroll2 = -40*(command_index);
                                }
                            }
                            break;
                        case SDLK_DOWN:
                            if (upperMode == 0 && cur_tile < 512-32) {
                                cur_tile += 32;
                            } else if (command_index != selectedPattern->numcommands-1) {
                                command_index++;
                                if (256-scroll2 < 40*(command_index+1)) {
                                    scroll2 = 256-40*(command_index+1);
                                }
                            }
                            break;
                        case SDLK_LEFTBRACKET:
                            if (upperMode != 0) scroll += 128;
                            break;
                        case SDLK_RIGHTBRACKET:
                            if (upperMode != 0) scroll -= 128;
                            break;
                        case SDLK_BACKSLASH:
                            scroll = 0;
                            break;
                        case SDLK_m:
                            lowerMode = !lowerMode;
                            break;
                        case SDLK_e:
                            upperMode = !upperMode;
                            break;
                        case SDLK_r:
                            if (lowerMode != 0 && cur_subsprite >= 0 && cur_subsprite < metasprites[cur_metasprite].numsprites) {
                                metasprites[cur_metasprite].subsprites[cur_subsprite].mirrorx ^= 1;
                                if (!metasprites[cur_metasprite].subsprites[cur_subsprite].mirrorx) {
                                    metasprites[cur_metasprite].subsprites[cur_subsprite].mirrory ^= 1;
                                }
                            }
                        case SDLK_c:
                            if (kstate[SDL_SCANCODE_LALT] && can_save) {
                                export_files();
                            }
                            break;
                        case SDLK_v:
                            if (kstate[SDL_SCANCODE_LALT]) {
                                import_files();
                            }
                            break;
                        case SDLK_KP_8:
                            if (upperMode == 1 && selectedPattern->commands[command_index].sizeY > 1) {
                                selectedPattern->commands[command_index].sizeY--;
                                if (selectedPattern->commands[command_index].type == DATABLOCK) {
                                    char *newarr = calloc(1,selectedPattern->commands[command_index].sizeX*selectedPattern->commands[command_index].sizeY);
                                    for (int i = 0; i < selectedPattern->commands[command_index].sizeY; i++) {
                                        for (int j = 0; j < selectedPattern->commands[command_index].sizeX; j++) {
                                            newarr[i*selectedPattern->commands[command_index].sizeX+j] = selectedPattern->commands[command_index].datablock[i*selectedPattern->commands[command_index].sizeX+j];
                                        }
                                    }
                                    free(selectedPattern->commands[command_index].datablock);
                                    selectedPattern->commands[command_index].datablock = newarr;
                                }
                            }
                            break;
                        case SDLK_KP_2:
                            if (upperMode == 1 && selectedPattern->commands[command_index].sizeY < (selectedPattern->commands[command_index].type == REFERENCE ? 4 : 16)) {
                                selectedPattern->commands[command_index].sizeY++;
                                if (selectedPattern->commands[command_index].type == DATABLOCK) {
                                    char *newarr = calloc(1,selectedPattern->commands[command_index].sizeX*selectedPattern->commands[command_index].sizeY);
                                    for (int i = 0; i < selectedPattern->commands[command_index].sizeY-1; i++) {
                                        for (int j = 0; j < selectedPattern->commands[command_index].sizeX; j++) {
                                            newarr[i*selectedPattern->commands[command_index].sizeX+j] = selectedPattern->commands[command_index].datablock[i*selectedPattern->commands[command_index].sizeX+j];
                                        }
                                    }
                                    free(selectedPattern->commands[command_index].datablock);
                                    selectedPattern->commands[command_index].datablock = newarr;
                                }
                            }
                            break;
                        case SDLK_KP_4:
                            if (upperMode == 1 && selectedPattern->commands[command_index].sizeX > 1) {
                                selectedPattern->commands[command_index].sizeX--;
                                if (selectedPattern->commands[command_index].type == DATABLOCK) {
                                    char *newarr = calloc(1,selectedPattern->commands[command_index].sizeX*selectedPattern->commands[command_index].sizeY);
                                    for (int i = 0; i < selectedPattern->commands[command_index].sizeY; i++) {
                                        for (int j = 0; j < selectedPattern->commands[command_index].sizeX; j++) {
                                            newarr[i*selectedPattern->commands[command_index].sizeX+j] = selectedPattern->commands[command_index].datablock[i*(selectedPattern->commands[command_index].sizeX+1)+j];
                                        }
                                    }
                                    free(selectedPattern->commands[command_index].datablock);
                                    selectedPattern->commands[command_index].datablock = newarr;
                                }
                            }
                            break;
                        case SDLK_KP_6:
                            if (upperMode == 1 && selectedPattern->commands[command_index].sizeX < 16) {
                                selectedPattern->commands[command_index].sizeX++;
                                if (selectedPattern->commands[command_index].type == DATABLOCK) {
                                    char *newarr = calloc(1,selectedPattern->commands[command_index].sizeX*selectedPattern->commands[command_index].sizeY);
                                    for (int i = 0; i < selectedPattern->commands[command_index].sizeY; i++) {
                                        for (int j = 0; j < selectedPattern->commands[command_index].sizeX-1; j++) {
                                            newarr[i*selectedPattern->commands[command_index].sizeX+j] = selectedPattern->commands[command_index].datablock[i*(selectedPattern->commands[command_index].sizeX-1)+j];
                                        }
                                    }
                                    free(selectedPattern->commands[command_index].datablock);
                                    selectedPattern->commands[command_index].datablock = newarr;
                                }
                            }
                            break;
                        case SDLK_w:
                            if ((lowerMode == 0 || cur_subsprite < 0 || cur_subsprite >= metasprites[cur_metasprite].numsprites) && upperMode == 1 && selectedPattern->commands[command_index].offY >= (selectedPattern->commands[command_index].type == MOVEMENT ? -3 : -7)) {
                                selectedPattern->commands[command_index].offY--;
                            }
                            break;
                        case SDLK_s:
                            if ((lowerMode == 0 || cur_subsprite < 0 || cur_subsprite >= metasprites[cur_metasprite].numsprites) && upperMode == 1 && selectedPattern->commands[command_index].offY < (selectedPattern->commands[command_index].type == MOVEMENT ? 3 : 7)) {
                                selectedPattern->commands[command_index].offY++;
                            }
                            break;
                        case SDLK_a:
                            if ((lowerMode == 0 || cur_subsprite < 0 || cur_subsprite >= metasprites[cur_metasprite].numsprites) && upperMode == 1 && selectedPattern->commands[command_index].offX >= (selectedPattern->commands[command_index].type == MOVEMENT ? -3 : -7)) {
                                selectedPattern->commands[command_index].offX--;
                            }
                            break;
                        case SDLK_d:
                            if ((lowerMode == 0 || cur_subsprite < 0 || cur_subsprite >= metasprites[cur_metasprite].numsprites) && upperMode == 1 && selectedPattern->commands[command_index].offX < (selectedPattern->commands[command_index].type == MOVEMENT ? 3 : 7)) {
                                selectedPattern->commands[command_index].offX++;
                            }
                            break;
                        case SDLK_i:
                            if (lowerMode == 1 && cur_subsprite >= 0 && metasprites[cur_metasprite].numsprites > cur_subsprite) {
                                metasprites[cur_metasprite].subsprites[cur_subsprite].rely--;
                                break;
                            }
                            if (yscroll > 0) yscroll--;
                            selectedPattern = &mainLevel[yscroll];
                            break;
                        case SDLK_k:
                            if (lowerMode == 1 && cur_subsprite >= 0 && metasprites[cur_metasprite].numsprites > cur_subsprite) {
                                metasprites[cur_metasprite].subsprites[cur_subsprite].rely++;
                                break;
                            }
                            if (yscroll < 3) yscroll++;
                            selectedPattern = &mainLevel[yscroll];
                            break;
                        case SDLK_j:
                            if (lowerMode == 1 && cur_subsprite >= 0 && metasprites[cur_metasprite].numsprites > cur_subsprite) {
                                metasprites[cur_metasprite].subsprites[cur_subsprite].relx--;
                            }
                            break;
                        case SDLK_l:
                            if (lowerMode == 1 && cur_subsprite >= 0 && metasprites[cur_metasprite].numsprites > cur_subsprite) {
                                metasprites[cur_metasprite].subsprites[cur_subsprite].relx++;
                            }
                            break;
                        case SDLK_n:
                            metasprites[cur_metasprite].numsprites++;
                            metasprites[cur_metasprite].subsprites = realloc(metasprites[cur_metasprite].subsprites, sizeof(Sprite)*metasprites[cur_metasprite].numsprites);
                            metasprites[cur_metasprite].subsprites[metasprites[cur_metasprite].numsprites-1].relx = 0;
                            metasprites[cur_metasprite].subsprites[metasprites[cur_metasprite].numsprites-1].rely = 0;
                            metasprites[cur_metasprite].subsprites[metasprites[cur_metasprite].numsprites-1].tile = 0;
                            metasprites[cur_metasprite].subsprites[metasprites[cur_metasprite].numsprites-1].palette = 0;
                            metasprites[cur_metasprite].subsprites[metasprites[cur_metasprite].numsprites-1].mirrorx = 0;
                            metasprites[cur_metasprite].subsprites[metasprites[cur_metasprite].numsprites-1].mirrory = 0;
                            cur_subsprite = metasprites[cur_metasprite].numsprites-1;
                            break;
                        case SDLK_LSHIFT:
                            newestDisplay = !newestDisplay;
                            break;
                        case SDLK_LCTRL:
                            clipNewer = !clipNewer;
                            break;
                        case SDLK_DELETE:
                        case SDLK_BACKSPACE:
                            if (lowerMode == 1) {
                                if (metasprites[cur_metasprite].numsprites != 1 && cur_subsprite >= 0) {
                                    metasprites[cur_metasprite].numsprites--;
                                    memcpy(&metasprites[cur_metasprite].subsprites[cur_subsprite], &metasprites[cur_metasprite].subsprites[metasprites[cur_metasprite].numsprites], sizeof(Sprite));
                                }
                                break;
                            }
                            if (selectedPattern->numcommands <= 1) break;
                            if (selectedPattern->commands[command_index].type == DATABLOCK) {
                                free(selectedPattern->commands[command_index].datablock);
                            }
                            for (int i = command_index; i < selectedPattern->numcommands-1; i++) {
                                selectedPattern->commands[i] = selectedPattern->commands[i+1];
                            }
                            selectedPattern->numcommands--;
                            selectedPattern->commands = realloc(selectedPattern->commands, sizeof(Command)*selectedPattern->numcommands);
                            if (command_index == selectedPattern->numcommands) {
                                command_index--;
                                if (-scroll2 > 40*(command_index)) {
                                    scroll2 = -40*(command_index);
                                }
                                if (256-scroll2 < 40*(command_index+1)) {
                                    scroll2 = 256-40*(command_index+1);
                                }
                            }
                            break;
                        case SDLK_RETURN:
                            selectedPattern->numcommands++;
                            selectedPattern->commands = realloc(selectedPattern->commands, sizeof(Command)*selectedPattern->numcommands);
                            for (int i = selectedPattern->numcommands-1; i > command_index; i--) {
                                selectedPattern->commands[i] = selectedPattern->commands[i-1];
                            }
                            selectedPattern->commands[command_index].type = RECTANGLE;
                            selectedPattern->commands[command_index].offX = 0;
                            selectedPattern->commands[command_index].offY = 0;
                            selectedPattern->commands[command_index].sizeX = 1;
                            selectedPattern->commands[command_index].sizeY = 1;
                            selectedPattern->commands[command_index].argument = 0;
                            break;
                        case SDLK_SPACE:
                            if (!upperMode) break;
                            switch(selectedPattern->commands[command_index].type) {
                                case POINT:
                                    selectedPattern->commands[command_index].type = RECTANGLE;
                                    selectedPattern->commands[command_index].argument = 0;
                                    selectedPattern->commands[command_index].sizeX = 1;
                                    selectedPattern->commands[command_index].sizeY = 1;
                                    break;
                                case RECTANGLE:
                                    selectedPattern->commands[command_index].type = DATABLOCK;
                                    selectedPattern->commands[command_index].datablock = calloc(1,1);
                                    selectedPattern->commands[command_index].sizeX = 1;
                                    selectedPattern->commands[command_index].sizeY = 1;
                                    break;
                                case DATABLOCK:
                                    free(selectedPattern->commands[command_index].datablock);
                                    selectedPattern->commands[command_index].type = REFERENCE;
                                    selectedPattern->commands[command_index].argument = 0;
                                    break;
                                case REFERENCE:
                                    selectedPattern->commands[command_index].type = MOVEMENT;
                                    selectedPattern->commands[command_index].sizeX = 0;
                                    selectedPattern->commands[command_index].sizeY = 0;
                                    break;
                                case MOVEMENT:
                                    selectedPattern->commands[command_index].type = POINT;
                                    selectedPattern->commands[command_index].argument = 0;
                                    break;
                            }
                            break;
                    }
                    break;
                case SDL_MOUSEBUTTONUP:
                case SDL_MOUSEBUTTONDOWN:
                case SDL_MOUSEMOTION:
                case SDL_MOUSEWHEEL:
                    ;
                    SDL_Rect mouserect;
                    SDL_Point mousepoint;
                    SDL_GetMouseState(&mousepoint.x, &mousepoint.y);
                    mouserect.x = CUR_TILE_X;
                    mouserect.y = CUR_TILE_Y;
                    mouserect.w = CUR_TILE_PSIZE;
                    mouserect.h = CUR_TILE_PSIZE;
                    for (int i = 0; i < 8; i++) {
                        for (int j = 0; j < 8; j++) {
                            if (SDL_PointInRect(&mousepoint, &mouserect) && upperMode == 0) {
                                onclick_curtile((i << 3) + j);
                            }
                            mouserect.x += mouserect.w;
                        }
                        mouserect.x = CUR_TILE_X;
                        mouserect.y += mouserect.h;
                    }
                    mouserect.x = PALETTES_X;
                    mouserect.y = PALETTES_Y;
                    mouserect.w = PALETTES_PSIZE;
                    mouserect.h = PALETTES_PSIZE;
                    for (int i = 0; i < 4; i++) {
                        for (int j = 0; j < 4; j++) {
                            if (SDL_PointInRect(&mousepoint, &mouserect) && upperMode == 0) {
                                onclick_palette((i << 2) + j);
                            }
                            mouserect.x += mouserect.w;
                        }
                        mouserect.x = PALETTES_X;
                        mouserect.y += mouserect.h;
                    }
                    mouserect.x = PATTERNS_X;
                    mouserect.y = PATTERNS_Y;
                    mouserect.w = PATTERNS_PSIZE;
                    mouserect.h = PATTERNS_PSIZE;
                    for (int i = 0; i < 16; i++) {
                        for (int j = 0; j < 32; j++) {
                            if (SDL_PointInRect(&mousepoint, &mouserect) && upperMode == 0) {
                                onclick_pattern((i << 5) + j);
                            }
                            mouserect.x += mouserect.w;
                        }
                        mouserect.x = PATTERNS_X;
                        mouserect.y += mouserect.h;
                    }
                    mouserect.x = METATILE_X;
                    mouserect.y = METATILE_Y;
                    mouserect.w = METATILE_PSIZE;
                    mouserect.h = METATILE_PSIZE;
                    for (int i = 0; i < 2; i++) {
                        for (int j = 0; j < 2; j++) {
                            if (SDL_PointInRect(&mousepoint, &mouserect)) {
                                onclick_metatile((i << 1) + j);
                            }
                            mouserect.x += mouserect.w;
                        }
                        mouserect.x = METATILE_X;
                        mouserect.y += mouserect.h;
                    }
                    mouserect.x = METAGRID_X;
                    mouserect.y = METAGRID_Y;
                    mouserect.w = METAGRID_PSIZE;
                    mouserect.h = METAGRID_PSIZE;
                    for (int i = 0; i < 8; i++) {
                        for (int j = 0; j < 8; j++) {
                            if (SDL_PointInRect(&mousepoint, &mouserect)) {
                                onclick_metagrid((i << 3) + j);
                            }
                            mouserect.x += mouserect.w;
                        }
                        mouserect.x = METAGRID_X;
                        mouserect.y += mouserect.h;
                    }
                    mouserect.x = 300;
                    mouserect.y = 10;
                    mouserect.w = 300;
                    mouserect.h = 256;
                    if (SDL_PointInRect(&mousepoint, &mouserect)) {
                        if (event.type == SDL_MOUSEWHEEL) {
                            if ((event.wheel.y > 0) != (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) && upperMode == 1) {
                                scroll2-=4;
                            } else {
                                scroll2+=4;
                            }
                        } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT && upperMode == 1) {
                            int realpos = event.button.y-10 - scroll2;
                            if (realpos/40 < selectedPattern->numcommands) {
                                command_index = realpos / 40;
                                if (-scroll2 > 40*(command_index)) {
                                    scroll2 = -40*(command_index);
                                }
                                if (256-scroll2 < 40*(command_index+1)) {
                                    scroll2 = 256-40*(command_index+1);
                                }
                            }
                        } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_RIGHT && upperMode == 1) {
                            int realpos = event.button.y-10 - scroll2;
                            if (realpos/40 < selectedPattern->numcommands) {
                                Command thecmd = selectedPattern->commands[command_index];
                                for (int i = command_index; i < selectedPattern->numcommands-1; i++) {
                                    selectedPattern->commands[i] = selectedPattern->commands[i+1];
                                }
                                for (int i = selectedPattern->numcommands-1; i > realpos/40; i--) {
                                    selectedPattern->commands[i] = selectedPattern->commands[i-1];
                                }
                                selectedPattern->commands[(realpos/40)] = thecmd;
                                command_index = realpos/40;
                                if (-scroll2 > 40*(command_index)) {
                                    scroll2 = -40*(command_index);
                                }
                                if (256-scroll2 < 40*(command_index+1)) {
                                    scroll2 = 256-40*(command_index+1);
                                }
                            }
                        }
                    }
                    mouserect.x = 610;
                    mouserect.y = 10;
                    mouserect.w = 300;
                    mouserect.h = 50;
                    if (SDL_PointInRect(&mousepoint, &mouserect) && event.type == SDL_MOUSEWHEEL) {
                        if ((event.wheel.y > 0) != (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)) {
                            if (selectedPattern == &patterns[0]) {
                                selectedPattern = &mainLevel[yscroll];
                            } else if (selectedPattern != &mainLevel[yscroll]) {
                                selectedPattern--;
                            }
                        } else {
                            if (selectedPattern == &mainLevel[yscroll]) {
                                selectedPattern = &patterns[0];
                            } else if (selectedPattern != &patterns[255]) {
                                selectedPattern++;
                            }
                        }
                        command_index = 0;
                        scroll2 = 0;
                    }
                    mouserect.y += 60;
                    if (selectedPattern != &mainLevel[yscroll] && SDL_PointInRect(&mousepoint, &mouserect) && event.type == SDL_MOUSEWHEEL) {
                        if ((event.wheel.y > 0) != (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)) {
                            if (selectedPattern->sizeX != 1) {
                                selectedPattern->sizeX--;
                            }
                        } else {
                            if (selectedPattern->sizeX != 16) {
                                selectedPattern->sizeX++;
                            }
                        }
                    } else if (SDL_PointInRect(&mousepoint, &mouserect) && event.type == SDL_MOUSEWHEEL) {
                        if ((event.wheel.y > 0) != (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)) {
                            if (yscroll > 0) {
                                yscroll--;
                            }
                        } else {
                            if (yscroll < 3) {
                                yscroll++;
                            }
                        }
                        selectedPattern = &mainLevel[yscroll];
                    }
                    mouserect.y += 60;
                    if (selectedPattern != &mainLevel[yscroll] && SDL_PointInRect(&mousepoint, &mouserect) && event.type == SDL_MOUSEWHEEL) {
                        if ((event.wheel.y > 0) != (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)) {
                            if (selectedPattern->sizeY != 1) {
                                selectedPattern->sizeY--;
                            }
                        } else {
                            if (selectedPattern->sizeY != 16) {
                                selectedPattern->sizeY++;
                            }
                        }
                    }
                    mouserect.w = 16;
                    mouserect.h = 16;
                    for (int i = 0; i < selectedPattern->sizeY; i++) {
                        for (int j = 0; j < selectedPattern->sizeX; j++) {
                            mouserect.x = 10+16*j+(selectedPattern == &mainLevel[yscroll] ? scroll : 0);
                            mouserect.y = 10+16*i;
                            if (SDL_PointInRect(&mousepoint, &mouserect)) {
                                for (int k = 0; k < num_added; k++) {
                                    if (last_pattern_added[k] == (i*selectedPattern->sizeX+j)) { 
                                        if (selectedPattern->commands[command_index].type == RECTANGLE && event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                                            selectedPattern->commands[command_index].argument = cur_metatile;
                                        } else if (selectedPattern->commands[command_index].type == DATABLOCK && event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                                            int hx = pos_history % selectedPattern->sizeX + selectedPattern->commands[command_index].offX;
                                            int hy = pos_history / selectedPattern->sizeX + selectedPattern->commands[command_index].offY;
                                            selectedPattern->commands[command_index].datablock[(i-hy)*selectedPattern->commands[command_index].sizeX+(j-hx)] = cur_metatile;
                                        } else if ((selectedPattern->commands[command_index].type == REFERENCE || selectedPattern->commands[command_index].type == POINT) && event.type == SDL_MOUSEWHEEL) {
                                            if ((event.wheel.y > 0) != (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)) {
                                                if (selectedPattern->commands[command_index].argument > 0) {
                                                    selectedPattern->commands[command_index].argument--;
                                                }
                                            } else {
                                                if (selectedPattern->commands[command_index].argument < 255) {
                                                    selectedPattern->commands[command_index].argument++;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    mouserect.x = 10;
                    mouserect.y = 500;
                    mouserect.w = 300;
                    mouserect.h = 40;
                    if (lowerMode != 0 && SDL_PointInRect(&mousepoint, &mouserect) && event.type == SDL_MOUSEWHEEL) {
                        if ((event.wheel.y > 0) != (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)) {
                            if (cur_metasprite > 0) {
                                cur_metasprite--;
                            }
                        } else {
                            if (cur_metasprite < 127) {
                                cur_metasprite++;
                            }
                        }
                    }
                    mouserect.w = 16;
                    mouserect.h = 16;
                    for (int i = 0; i < metasprites[cur_metasprite].numsprites; i++) {
                        mouserect.x = 500+metasprites[cur_metasprite].subsprites[i].relx*2;
                        mouserect.y = 600+metasprites[cur_metasprite].subsprites[i].rely*2;
                        if (SDL_PointInRect(&mousepoint, &mouserect) && event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                            if (cur_subsprite == i) {
                                cur_subsprite = -1;
                            } else {
                                cur_subsprite = i;
                            }
                        } else if (SDL_PointInRect(&mousepoint, &mouserect) && event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_RIGHT) {
                            metasprites[cur_metasprite].subsprites[i].tile = cur_tile;
                            metasprites[cur_metasprite].subsprites[i].palette = tiles[cur_tile].lastPalette;
                        } else if (SDL_PointInRect(&mousepoint, &mouserect) && event.type == SDL_MOUSEWHEEL) {
                            if ((event.wheel.y > 0) != (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)) {
                                metasprites[cur_metasprite].subsprites[i].palette--;
                            } else {
                                metasprites[cur_metasprite].subsprites[i].palette++;
                            }
                            metasprites[cur_metasprite].subsprites[i].palette &= 3;
                        }
                    }
                    break;
            }
        }
        SDL_SetRenderDrawColor(renderer, 150, 150, 150, 0);
        SDL_RenderClear(renderer);
        char buffer[64];
        if (upperMode == 0) {
            for (int i = 0; i < 512; i++) {
                draw_tile(&tiles[i], PATTERNS_X+((i & 0b11111) << 4), PATTERNS_Y+((i >> 5) << 4), PATTERNS_PSIZE, PATTERNS_PSIZE);
            }
            rect.x = PALETTES_X;
            rect.y = PALETTES_Y;
            rect.w = PALETTES_PSIZE;
            rect.h = PALETTES_PSIZE;
            for (int i = 0; i < 4; i++) {
                for (int j = 0; j < 4; j++) {
                    SDL_SetRenderDrawColor(renderer, nes_palette[palettes[i][j]].r, nes_palette[palettes[i][j]].g, nes_palette[palettes[i][j]].b, nes_palette[palettes[i][j]].a);
                    SDL_RenderFillRect(renderer, &rect);
                    if (infoDisplay) {
                        sprintf(buffer, "$%02x", palettes[i][j]);
                        draw_text(rect.x+1, rect.y+5, buffer);
                    }
                    rect.x += PALETTES_PSIZE;
                }
                rect.x = PALETTES_X;
                rect.y += PALETTES_PSIZE;
            }
            rect.x = PALETTES_X-1;
            rect.y = PALETTES_Y-1 + 32*selectedPalette;
            SDL_QueryTexture(aroundPalBox, NULL, NULL, &rect.w, &rect.h);
            SDL_RenderCopy(renderer, aroundPalBox, NULL, &rect);
            rect.x = PALETTES_X-3 + 32*selectedColor;
            rect.y -= 2;
            SDL_QueryTexture(aroundColBox, NULL, NULL, &rect.w, &rect.h);
            SDL_RenderCopy(renderer, aroundColBox, NULL, &rect);
            draw_tile(&tiles[cur_tile], CUR_TILE_X, CUR_TILE_Y, CUR_TILE_PSIZE*8, CUR_TILE_PSIZE*8);
            rect.x = PATTERNS_X+((cur_tile & 0b11111) << 4) - 1;
            rect.y = PATTERNS_Y+((cur_tile >> 5) << 4) - 1;
            SDL_QueryTexture(aroundBox, NULL, NULL, &rect.w, &rect.h);
            SDL_RenderCopy(renderer, aroundBox, NULL, &rect);
        } else {
            update_pattern_cache(selectedPattern, command_index+1);
            rect.x = 10;
            rect.y = 10;
            rect.w = 16*16;
            rect.h = 16*16;
            SDL_RenderSetClipRect(renderer, &rect);
            draw_pattern(selectedPattern);
            if (newestDisplay) {
                SDL_Color red = {255, 0, 0, 120};
                SDL_Color darkred = {150, 0, 0, 240};
                for (int i = 0; i < num_added; i++) {
                    rect.x = 16*(last_pattern_added[i] % selectedPattern->sizeX)+10+(selectedPattern == &mainLevel[yscroll] ? scroll : 0);
                    rect.y = 16*(last_pattern_added[i] / selectedPattern->sizeX)+10;
                    rect.w = 16;
                    rect.h = 16;
                    SDL_SetRenderDrawColor(renderer, red.r, red.g, red.b, red.a);
                    SDL_RenderFillRect(renderer, &rect);
                    SDL_SetRenderDrawColor(renderer, darkred.r, darkred.g, darkred.b, darkred.a);
                    SDL_RenderDrawRect(renderer, &rect);
                }
                rect.x = 10;
                rect.y = 10;
                rect.w = 16*16;
                rect.h = 16*16;
                SDL_RenderSetClipRect(renderer, &rect);
                rect.x = 16*(upcoming_pos % selectedPattern->sizeX)+10+(selectedPattern == &mainLevel[yscroll] ? scroll : 0);
                rect.y = 16*(upcoming_pos / selectedPattern->sizeX)+10;
                rect.w = 16;
                rect.h = 16;
                SDL_SetRenderDrawColor(renderer, 150, 150, 0, 240);
                SDL_RenderDrawRect(renderer, &rect);
                SDL_SetRenderDrawColor(renderer, 255, 255, 0, 120);
                SDL_RenderFillRect(renderer, &rect);
                int oldx = rect.x;
                int oldy = rect.y;
                rect.x = 16*(pos_history % selectedPattern->sizeX)+10+(selectedPattern == &mainLevel[yscroll] ? scroll : 0);
                rect.y = 16*(pos_history / selectedPattern->sizeX)+10;
                rect.w = 16;
                rect.h = 16;
                SDL_SetRenderDrawColor(renderer, 0, 0, 255, 120);
                SDL_RenderFillRect(renderer, &rect);
                SDL_SetRenderDrawColor(renderer, 0, 0, 150, 240);
                SDL_RenderDrawRect(renderer, &rect);
                SDL_SetRenderDrawColor(renderer, 0, 255, 0, 240);
                SDL_RenderDrawLine(renderer, oldx+8, oldy+8, rect.x+8, rect.y+8);
                SDL_RenderDrawLine(renderer, oldx+9, oldy+8, rect.x+9, rect.y+8);
                SDL_RenderDrawLine(renderer, oldx+7, oldy+8, rect.x+7, rect.y+8);
                SDL_RenderDrawLine(renderer, oldx+8, oldy+9, rect.x+8, rect.y+9);
                SDL_RenderDrawLine(renderer, oldx+8, oldy+7, rect.x+8, rect.y+7);
            }
            rect.x = 300;
            rect.y = 10;
            rect.w = 300;
            rect.h = 256;
            SDL_RenderSetClipRect(renderer, &rect);
            for (int i = 0; i < selectedPattern->numcommands; i++) {
                draw_command(selectedPattern->commands[i], i);
            }
            rect.x = 300-4;
            rect.y = 10+command_index*40-4+scroll2;
            rect.w = 308;
            rect.h = 40;
            SDL_SetRenderDrawColor(renderer, 0, 0, 255, 120);
            SDL_RenderFillRect(renderer, &rect);
            SDL_SetRenderDrawColor(renderer, 0, 0, 150, 240);
            SDL_RenderDrawRect(renderer, &rect);
            SDL_RenderSetClipRect(renderer, NULL);
            if (selectedPattern == &mainLevel[yscroll]) {
                sprintf(buffer, "Main Level");
            } else {
                sprintf(buffer, "Pattern: %ld", selectedPattern-(Pattern*)&patterns);
            }
            draw_text3(610, 10, buffer);
            if (selectedPattern != &mainLevel[yscroll]) {
                sprintf(buffer, "Size in X: %d", selectedPattern->sizeX);
                draw_text3(610, 70, buffer);
            } else {
                sprintf(buffer, "Y slice: %d", yscroll);
                draw_text3(610, 70, buffer);
            }
            if (selectedPattern != &mainLevel[yscroll]) {
                sprintf(buffer, "Size in Y: %d", selectedPattern->sizeY);
                draw_text3(610, 130, buffer);
            }
            
        }
        if (lowerMode == 0) {
            draw_metatile(&metatiles[cur_metatile], METATILE_X, METATILE_Y, METATILE_PSIZE*2, METATILE_PSIZE*2);
            if (infoDisplay) {
                for (int i = 0; i < 4; i++) {
                    sprintf(buffer, "$%02x", metatiles[cur_metatile].subtiles[i]);
                    draw_text(METATILE_X + 16 + METATILE_PSIZE*(i & 1), METATILE_Y + 16 + METATILE_PSIZE*(i >> 1), buffer);
                }
            }
            for (int i = 0; i < 64; i++) {
                draw_metatile(&metatiles[i], METAGRID_X+METAGRID_PSIZE*(i & 7), METAGRID_Y+METAGRID_PSIZE*(i >> 3), METAGRID_PSIZE, METAGRID_PSIZE);
            }
            rect.x = METAGRID_X + 32*(cur_metatile & 7)-1;
            rect.y = METAGRID_Y + 32*(cur_metatile >> 3)-1; 
            SDL_QueryTexture(aroundMetagridBox, NULL, NULL, &rect.w, &rect.h);
            SDL_RenderCopy(renderer, aroundMetagridBox, NULL, &rect);
        } else {
            sprintf(buffer, "Sprite selected: %d\n", cur_metasprite);
            draw_text3(10, 500, buffer);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderDrawLine(renderer, 500, 500, 500, 700);
            SDL_RenderDrawLine(renderer, 400, 600, 600, 600);
            draw_metasprite(&metasprites[cur_metasprite], 500, 600);
            if (cur_subsprite >= 0 && cur_subsprite < metasprites[cur_metasprite].numsprites) {
                rect.x = metasprites[cur_metasprite].subsprites[cur_subsprite].relx*2 + 500;
                rect.y = metasprites[cur_metasprite].subsprites[cur_subsprite].rely*2 + 600;
                SDL_QueryTexture(aroundSpriteBox, NULL, NULL, &rect.w, &rect.h);
                SDL_RenderCopy(renderer, aroundSpriteBox, NULL, &rect);
            }
        }
        sprintf(buffer, "Last bytes: %d\n", num_file_bytes());
        draw_text3(500, 750, buffer);
        SDL_RenderPresent(renderer);
    }
}