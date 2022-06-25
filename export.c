#include <stdio.h>
#include <stdlib.h>

int serializePattern(FILE *pattern, char *buffer, int includeHeader) {
    int numcommands = fgetc(pattern);
    int psizeX = fgetc(pattern)-1;
    int psizeY = fgetc(pattern)-1;
    char *orig = buffer;
    if (includeHeader) {
        *(buffer++) = (psizeX << 4) | psizeY;
    }
    *(buffer++) = numcommands;
    for (int i = 0; i < numcommands; i++) {
        int cmdType = fgetc(pattern);
        int argument, offX, offY, sizeX, sizeY;
        switch(cmdType) {
            case 0: // POINT
                argument = fgetc(pattern);
                offX = fgetc(pattern) & 15;
                offY = fgetc(pattern) & 15;
                *(buffer++) = argument;
                *(buffer++) = (offX << 4) | offY;
                break;
            case 1: // RECTANGLE
                argument = fgetc(pattern);
                offX = fgetc(pattern) & 15;
                offY = fgetc(pattern) & 15;
                sizeX = fgetc(pattern);
                sizeY = fgetc(pattern);
                *(buffer++) = argument | 0b10000000;
                *(buffer++) = (offX << 4) | offY;
                *(buffer++) = ((sizeX-1) << 4) | (sizeY-1);
                break;
            case 2: // DATABLOCK
                offX = fgetc(pattern) & 15;
                offY = fgetc(pattern) & 15;
                sizeX = fgetc(pattern);
                sizeY = fgetc(pattern);
                *(buffer++) = 0b01000000;
                *(buffer++) = (offX << 4) | offY;
                *(buffer++) = ((sizeX-1) << 4) | (sizeY-1);
                for (int j = 0; j < sizeX*sizeY; j++) {
                    *(buffer++) = fgetc(pattern);
                }
                break;
            case 3: // REFERENCE
                argument = fgetc(pattern);
                offX = fgetc(pattern) & 15;
                offY = fgetc(pattern) & 15;
                sizeX = fgetc(pattern);
                sizeY = fgetc(pattern);
                *(buffer++) = ((sizeX-1) << 2) | (sizeY-1) | 0b11000000;
                *(buffer++) = (offX << 4) | offY;
                *(buffer++) = argument;
                break;
            case 4: // MOVEMENT
                offX = ((signed char)fgetc(pattern)) & 7;
                offY = ((signed char)fgetc(pattern)) & 7;
                *(buffer++) = (offX << 3) | (offY) | 0b01000000;
                break;
        }
    }
    return buffer - orig;
}

int main() {
    FILE *binary = fopen("level.bin", "w");
    char buffer[1024];
    int num_good_patterns = 0;
    char fname[64];
    FILE *temp;
    char which_good_patterns[256];
    for (int i = 0; i < 256; i++) {
        sprintf(fname, "level/patterns/pattern%d", i);
        temp = fopen(fname, "r");
        if (fgetc(temp) == 1 && fgetc(temp) == 1) {
            which_good_patterns[i] = 0;
            continue;
        } else {
            num_good_patterns++;
            which_good_patterns[i] = 1;
        }
    }
    int mainLevelOffset = (num_good_patterns << 1) + 14;
    fputc(mainLevelOffset & 0xff, binary);
    fputc(0xe0 | (mainLevelOffset >> 8), binary);
    fputc(0xe0, binary);
    fputc(0xe0, binary); // TODO METASPRITES
    fputc(0xe0, binary);
    fputc(0xe0, binary);
    fputc(0xe0, binary);
    fputc(0xe0, binary);
    fputc(0xe0, binary);
    fputc(0xe0, binary);
    fputc(0xe0, binary);
    fputc(0xe0, binary);
    fputc(0xe0, binary); // METASPRITE POINTER
    fputc(0xe0, binary);
    for (int i = 0; i < num_good_patterns; i++) {
        fputc(0x00, binary); // to be sought later
        fputc(0x00, binary);
    }
    temp = fopen("level/mainlevel", "r");
    int mainLevelLength = serializePattern(temp, buffer, 0);
    for (int i = 0; i < mainLevelLength; i++) {
        fputc(buffer[i], binary);
    }
    int runningOffset = mainLevelOffset + mainLevelLength;
    for (int i = 0; i < 256; i++) {
        if (!which_good_patterns[i]) continue;
        sprintf(fname, "level/patterns/pattern%d", i);
        temp = fopen(fname, "r");
        int patSize = serializePattern(temp, buffer, 1);
        fseek(binary, 14 + (i << 1), SEEK_SET);
        fputc(runningOffset & 0xff, binary);
        fputc(0xe0 | (runningOffset >> 8), binary);
        fseek(binary, 0, SEEK_END);
        runningOffset += patSize;
        //printf("pattern number %d is the %d byte\n", i, patSize);
        for (int j = 0; j < patSize; j++) {
            fputc(buffer[j], binary);
        }
    }
    char buffer1[64];
    char buffer2[64];
    char buffer3[64];
    char buffer4[64];
    char buffer5[64];
    int num_good_metas = 0;
    int allzeros = 1;
    for (int i = 0; i < 64; i++) {
        sprintf(fname, "level/ltiles/ltile%d", i);
        temp = fopen(fname, "r");
        char tbuffer[4];
        tbuffer[0] = fgetc(temp);
        tbuffer[1] = fgetc(temp);
        tbuffer[2] = fgetc(temp);
        tbuffer[3] = fgetc(temp);
        if (tbuffer[0] || tbuffer[1] || tbuffer[2] || tbuffer[3]) {
            num_good_metas++;
        } else if (allzeros) {
            allzeros = 0;
            num_good_metas++;
        } else {
            continue;
        }
        int pal;
        sprintf(fname, "level/tiles/tile%d", tbuffer[0]);
        temp = fopen(fname, "r");
        pal = fgetc(temp);
        buffer1[num_good_metas-1] = tbuffer[0];
        buffer2[num_good_metas-1] = tbuffer[1];
        buffer3[num_good_metas-1] = tbuffer[2];
        buffer4[num_good_metas-1] = tbuffer[3];
        buffer5[num_good_metas-1] = pal;
    }
    int off1 = runningOffset;
    int off2 = runningOffset+num_good_metas;
    int off3 = runningOffset+num_good_metas*2;
    int off4 = runningOffset+num_good_metas*3;
    int off5 = runningOffset+num_good_metas*4;
    runningOffset = off5+num_good_metas;
    for (int i = 0; i < num_good_metas; i++) {
        fputc(buffer1[i], binary);
    }
    for (int i = 0; i < num_good_metas; i++) {
        fputc(buffer2[i], binary);
    }
    for (int i = 0; i < num_good_metas; i++) {
        fputc(buffer3[i], binary);
    }
    for (int i = 0; i < num_good_metas; i++) {
        fputc(buffer4[i], binary);
    }
    for (int i = 0; i < num_good_metas; i++) {
        fputc(buffer5[i], binary);
    }
    fseek(binary, 2, SEEK_SET);
    fputc(off1 & 0xff, binary);
    fputc((off1 >> 8) | 0xe0, binary);
    fputc(off2 & 0xff, binary);
    fputc((off2 >> 8) | 0xe0, binary);
    fputc(off3 & 0xff, binary);
    fputc((off3 >> 8) | 0xe0, binary);
    fputc(off4 & 0xff, binary);
    fputc((off4 >> 8) | 0xe0, binary);
    fputc(off5 & 0xff, binary);
    fputc((off5 >> 8) | 0xe0, binary);
    fputc(runningOffset & 0xff, binary);
    fputc((runningOffset >> 8) | 0xe0, binary);
    fseek(binary, 0, SEEK_END);
    int mes_offsets[128];
    int mes_size = 0;
    int rbad = 0;
    for (int i = 0; i < 128; i++) {
        sprintf(fname, "level/sprites/sprite%d", i);
        temp = fopen(fname, "r");
        int count = fgetc(temp);
        mes_offsets[i] = mes_size;
        int bad = 0;
        buffer[mes_size+1] = count;
        for (int j = 0; j < count; j++) {
            char mirrorx = fgetc(temp); 
            char mirrory = fgetc(temp); 
            char relx = fgetc(temp); 
            char rely = fgetc(temp); 
            char palette = fgetc(temp); 
            char tile = fgetc(temp);
            buffer[mes_size+1] = relx;
            buffer[mes_size+2] = rely;
            buffer[mes_size+3] = tile;
            buffer[mes_size+4] = (mirrorx << 7) | (mirrory << 6) | palette;
            if (count == 1 && tile == 0 && rbad) {
                bad = 1;
                mes_offsets[i] = -1;
                continue;
            } else if (count == 1 && tile == 0) {
                rbad = 1;
            }
            mes_size += 4;
        }
        if (!bad) mes_size++;
    }
    for (int i = 0; i < 128; i++) {
        if (mes_offsets[i] == -1) continue;
        fputc(mes_offsets[i] & 0xff, binary);
        fputc((mes_offsets[i] >> 8) | 0xe0, binary);
    }
    for (int i = 0; i < mes_size; i++) {
        fputc(buffer[i], binary);
    }
    fclose(binary);
    binary = fopen("output.chr", "w");
    for (int i = 0; i < 512; i++) {
        sprintf(fname, "level/tiles/tile%d", i);
        temp = fopen(fname, "r");
        fgetc(temp);
        char bitplane0[64];
        char bitplane1[64];
        for (int j = 0; j < 64; j++) {
            int v = fgetc(temp);
            bitplane0[j] = v & 1;
            bitplane1[j] = v >> 1;
        }
        for (int j = 0; j < 8; j++) {
            int b0 = 0;
            for (int k = 0; k < 8; k++) {
                b0 <<= 1;
                b0 |= bitplane0[8*j+k];
            }
            fputc(b0, binary);
        }
        for (int j = 0; j < 8; j++) {
            int b1 = 0;
            for (int k = 0; k < 8; k++) {
                b1 <<= 1;
                b1 |= bitplane1[8*j+k];
            }
            fputc(b1, binary);
        }
    }
    fclose(binary);
}
