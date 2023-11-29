/*
 * collide.c
 * program which demonstrates sprites colliding with tiles
 */

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 160
#define SOLID_TILE_INDEX 10

#define CHARACTER_SPRITE_HEIGHT 32
#define CHARACTER_SPRITE_WIDTH  16
#define TILE_SIZE 8 // Each tile is 8x8 pixels
#define BACKGROUND_WIDTH_TILES 32 // Width of the background in tiles
#define TOTAL_BACKGROUND_WIDTH (BACKGROUND_WIDTH_TILES * TILE_SIZE) // Total width in pixels
#define MAX_SCROLL (TOTAL_BACKGROUND_WIDTH - SCREEN_WIDTH)
/* include the background image we are using */
#include "tiles.h"

/* include the sprite image we are using */
#include "character.h"

/* include the tile map we are using */

#include "background.h"

#include "background2.h"
#define END_OF_FIRST_BACKGROUND 80

/* the tile mode flags needed for display control register */
#define MODE0 0x00
#define BG0_ENABLE 0x100

/* flags to set sprite handling in display control register */
#define SPRITE_MAP_2D 0x0
#define SPRITE_MAP_1D 0x40
#define SPRITE_ENABLE 0x1000


/* the control registers for the four tile layers */
volatile unsigned short* bg0_control = (volatile unsigned short*) 0x4000008;

/* palette is always 256 colors */
#define PALETTE_SIZE 256

/* there are 128 sprites on the GBA */
#define NUM_SPRITES 128

/* the display control pointer points to the gba graphics register */
volatile unsigned long* display_control = (volatile unsigned long*) 0x4000000;

/* the memory location which controls sprite attributes */
volatile unsigned short* sprite_attribute_memory = (volatile unsigned short*) 0x7000000;

/* the memory location which stores sprite image data */
volatile unsigned short* sprite_image_memory = (volatile unsigned short*) 0x6010000;

/* the address of the color palettes used for backgrounds and sprites */
volatile unsigned short* bg_palette = (volatile unsigned short*) 0x5000000;
volatile unsigned short* sprite_palette = (volatile unsigned short*) 0x5000200;

/* the button register holds the bits which indicate whether each button has
 * been pressed - this has got to be volatile as well
 */
volatile unsigned short* buttons = (volatile unsigned short*) 0x04000130;

/* scrolling registers for backgrounds */
volatile short* bg0_x_scroll = (unsigned short*) 0x4000010;
volatile short* bg0_y_scroll = (unsigned short*) 0x4000012;
int xscroll1 = 0; // Scrolling offset for the first background
int xscroll2 = 0; // Scrolling offset for the second background
int currentBackground = 0; // 0 for first background, 1 for second
/* the bit positions indicate each button - the first bit is for A, second for
 * B, and so on, each constant below can be ANDED into the register to get the
 * status of any one button */
#define BUTTON_A (1 << 0)
#define BUTTON_B (1 << 1)
#define BUTTON_SELECT (1 << 2)
#define BUTTON_START (1 << 3)
#define BUTTON_RIGHT (1 << 4)
#define BUTTON_LEFT (1 << 5)
#define BUTTON_UP (1 << 6)
#define BUTTON_DOWN (1 << 7)
#define BUTTON_R (1 << 8)
#define BUTTON_L (1 << 9)

/* the scanline counter is a memory cell which is updated to indicate how
 * much of the screen has been drawn */
volatile unsigned short* scanline_counter = (volatile unsigned short*) 0x4000006;

/* wait for the screen to be fully drawn so we can do something during vblank */
void wait_vblank() {
    /* wait until all 160 lines have been updated */
    while (*scanline_counter < 160) { }
}

/* this function checks whether a particular button has been pressed */
unsigned char button_pressed(unsigned short button) {
    /* and the button register with the button constant we want */
    unsigned short pressed = *buttons & button;

    /* if this value is zero, then it's not pressed */
    if (pressed == 0) {
        return 1;
    } else {
        return 0;
    }
}

/* return a pointer to one of the 4 character blocks (0-3) */
volatile unsigned short* char_block(unsigned long block) {
    /* they are each 16K big */
    return (volatile unsigned short*) (0x6000000 + (block * 0x4000));
}

/* return a pointer to one of the 32 screen blocks (0-31) */
volatile unsigned short* screen_block(unsigned long block) {
    /* they are each 2K big */
    return (volatile unsigned short*) (0x6000000 + (block * 0x800));
}

/* flag for turning on DMA */
#define DMA_ENABLE 0x80000000

/* flags for the sizes to transfer, 16 or 32 bits */
#define DMA_16 0x00000000
#define DMA_32 0x04000000

/* pointer to the DMA source location */
volatile unsigned int* dma_source = (volatile unsigned int*) 0x40000D4;

/* pointer to the DMA destination location */
volatile unsigned int* dma_destination = (volatile unsigned int*) 0x40000D8;

/* pointer to the DMA count/control */
volatile unsigned int* dma_count = (volatile unsigned int*) 0x40000DC;

/* copy data using DMA */
void memcpy16_dma(unsigned short* dest, unsigned short* source, int amount) {
    *dma_source = (unsigned int) source;
    *dma_destination = (unsigned int) dest;
    *dma_count = amount | DMA_16 | DMA_ENABLE;
}

/* function to setup background 0 for this program */
void setup_background() {

    /* load the palette from the image into palette memory*/
    memcpy16_dma((unsigned short*) bg_palette, (unsigned short*) tiles_palette, PALETTE_SIZE);

    /* load the image into char block 0 */
    memcpy16_dma((unsigned short*) char_block(0), (unsigned short*) tiles_data,
            (tiles_width * tiles_height) / 2);

    /* set all control the bits in this register */
    *bg0_control = 0 |    /* priority, 0 is highest, 3 is lowest */
        (0 << 2)  |       /* the char block the image data is stored in */
        (0 << 6)  |       /* the mosaic flag */
        (1 << 7)  |       /* color mode, 0 is 16 colors, 1 is 256 colors */
        (16 << 8) |       /* the screen block the tile data is stored in */
        (1 << 13) |       /* wrapping flag */
        (0 << 14);        /* bg size, 0 is 256x256 */

    /* load the tile data into screen block 16 */
    memcpy16_dma((unsigned short*) screen_block(16), (unsigned short*) background, background_width * background_height);
}

void setup_background2() {
    memcpy16_dma((unsigned short*) screen_block(16), (unsigned short*) background2, background2_width * background2_height);
}

/* just kill time */
void delay(unsigned int amount) {
    for (int i = 0; i < amount * 10; i++);
}

/* a sprite is a moveable image on the screen */
struct Sprite {
    unsigned short attribute0;
    unsigned short attribute1;
    unsigned short attribute2;
    unsigned short attribute3;
};

/* array of all the sprites available on the GBA */
struct Sprite sprites[NUM_SPRITES];
int next_sprite_index = 0;

/* the different sizes of sprites which are possible */
enum SpriteSize {
    SIZE_8_8,
    SIZE_16_16,
    SIZE_32_32,
    SIZE_64_64,
    SIZE_16_8,
    SIZE_32_8,
    SIZE_32_16,
    SIZE_64_32,
    SIZE_8_16,
    SIZE_8_32,
    SIZE_16_32,
    SIZE_32_64
};

/* function to initialize a sprite with its properties, and return a pointer */
struct Sprite* sprite_init(int x, int y, enum SpriteSize size,
        int horizontal_flip, int vertical_flip, int tile_index, int priority) {

    /* grab the next index */
    int index = next_sprite_index++;

    /* setup the bits used for each shape/size possible */
    int size_bits, shape_bits;
    switch (size) {
        case SIZE_8_8:   size_bits = 0; shape_bits = 0; break;
        case SIZE_16_16: size_bits = 1; shape_bits = 0; break;
        case SIZE_32_32: size_bits = 2; shape_bits = 0; break;
        case SIZE_64_64: size_bits = 3; shape_bits = 0; break;
        case SIZE_16_8:  size_bits = 0; shape_bits = 1; break;
        case SIZE_32_8:  size_bits = 1; shape_bits = 1; break;
        case SIZE_32_16: size_bits = 2; shape_bits = 1; break;
        case SIZE_64_32: size_bits = 3; shape_bits = 1; break;
        case SIZE_8_16:  size_bits = 0; shape_bits = 2; break;
        case SIZE_8_32:  size_bits = 1; shape_bits = 2; break;
        case SIZE_16_32: size_bits = 2; shape_bits = 2; break;
        case SIZE_32_64: size_bits = 3; shape_bits = 2; break;
    }

    int h = horizontal_flip ? 1 : 0;
    int v = vertical_flip ? 1 : 0;

    /* set up the first attribute */
    sprites[index].attribute0 = y |             /* y coordinate */
        (0 << 8) |          /* rendering mode */
        (0 << 10) |         /* gfx mode */
        (0 << 12) |         /* mosaic */
        (1 << 13) |         /* color mode, 0:16, 1:256 */
        (shape_bits << 14); /* shape */

    /* set up the second attribute */
    sprites[index].attribute1 = x |             /* x coordinate */
        (0 << 9) |          /* affine flag */
        (h << 12) |         /* horizontal flip flag */
        (v << 13) |         /* vertical flip flag */
        (size_bits << 14);  /* size */

    /* setup the second attribute */
    sprites[index].attribute2 = tile_index |   // tile index */
        (priority << 10) | // priority */
        (0 << 12);         // palette bank (only 16 color)*/

    /* return pointer to this sprite */
    return &sprites[index];
}

/* update all of the spries on the screen */
void sprite_update_all() {
    /* copy them all over */
    memcpy16_dma((unsigned short*) sprite_attribute_memory, (unsigned short*) sprites, NUM_SPRITES * 4);
}

/* setup all sprites */
void sprite_clear() {
    /* clear the index counter */
    next_sprite_index = 0;

    /* move all sprites offscreen to hide them */
    for(int i = 0; i < NUM_SPRITES; i++) {
        sprites[i].attribute0 = SCREEN_HEIGHT;
        sprites[i].attribute1 = SCREEN_WIDTH;
    }
}

/* set a sprite postion */
void sprite_position(struct Sprite* sprite, int x, int y) {
    /* clear out the y coordinate */
    sprite->attribute0 &= 0xff00;

    /* set the new y coordinate */
    sprite->attribute0 |= (y & 0xff);

    /* clear out the x coordinate */
    sprite->attribute1 &= 0xfe00;

    /* set the new x coordinate */
    sprite->attribute1 |= (x & 0x1ff);
}

/* move a sprite in a direction */
void sprite_move(struct Sprite* sprite, int dx, int dy) {
    /* get the current y coordinate */
    int y = sprite->attribute0 & 0xff;

    /* get the current x coordinate */
    int x = sprite->attribute1 & 0x1ff;

    /* move to the new location */
    sprite_position(sprite, x + dx, y + dy);
}

/* change the vertical flip flag */
void sprite_set_vertical_flip(struct Sprite* sprite, int vertical_flip) {
    if (vertical_flip) {
        /* set the bit */
        sprite->attribute1 |= 0x2000;
    } else {
        /* clear the bit */
        sprite->attribute1 &= 0xdfff;
    }
}

/* change the vertical flip flag */
void sprite_set_horizontal_flip(struct Sprite* sprite, int horizontal_flip) {
    if (horizontal_flip) {
        /* set the bit */
        sprite->attribute1 |= 0x1000;
    } else {
        /* clear the bit */
        sprite->attribute1 &= 0xefff;
    }
}

/* change the tile offset of a sprite */
void sprite_set_offset(struct Sprite* sprite, int offset) {
    /* clear the old offset */
    sprite->attribute2 &= 0xfc00;

    /* apply the new one */
    sprite->attribute2 |= (offset & 0x03ff);
}

/* setup the sprite image and palette */
void setup_sprite_image() {
    /* load the palette from the image into palette memory*/
    memcpy16_dma((unsigned short*) sprite_palette, (unsigned short*) character_palette, PALETTE_SIZE);

    /* load the image into sprite image memory */
    memcpy16_dma((unsigned short*) sprite_image_memory, (unsigned short*) character_data, (character_width * character_height) / 2);
}

/* a struct for the koopa's logic and behavior */
struct Character {
    /* the actual sprite attribute info */
    struct Sprite* sprite;

    /* the x and y postion in pixels */
    int x, y;

    /* the koopa's y velocity in 1/256 pixels/second */
    int yvel;

    /* the koopa's y acceleration in 1/256 pixels/second^2 */
    int gravity; 

    /* which frame of the animation he is on */
    int frame;

    /* the number of frames to wait before flipping */
    int animation_delay;

    /* the animation counter counts how many frames until we flip */
    int counter;

    /* whether the koopa is moving right now or not */
    int move;

    /* the number of pixels away from the edge of the screen the koopa stays */
    int border;

    /* if the koopa is currently falling */
    int falling;

    /*number of jumps left*/
    int jumps;
};

/* initialize the koopa */
void character_init(struct Character* character) {
    character->x = 0;
    character->y = 15;
    character->yvel = 0;
    character->gravity = 50;
    character->border = 0;
    character->frame = 0;
    character->move = 0;
    character->counter = 0;
    character->falling = 0;
    character->animation_delay = 8;
    character->sprite = sprite_init(character->x, character->y, SIZE_16_32, 0, 0, character->frame, 0);
    character->jumps = 10;
}

/* move the koopa left or right returns if it is at edge of the screen */
int character_left(struct Character* character) {
    /* face left */
    sprite_set_horizontal_flip(character->sprite, 1);
    character->move = 1;
    if (currentBackground == 0 && xscroll1 <= 0 && character->x <= character->border) {
        return 0;  // Prevent moving left at the start of the first background
    }

    /* if we are at the left end, just scroll the screen */
    if (character->x < character->border) {
        return 1;
    } else {
        /* else move left */
        character->x--;
        return 0;
    }
}
int character_right(struct Character* character) {
    sprite_set_horizontal_flip(character->sprite, 0);
    character->move = 1;

    
    if (currentBackground == 0) {
        if (xscroll1 < MAX_SCROLL) {
   
            character->x++;
            return 1;
        } else if (character->x < (SCREEN_WIDTH - CHARACTER_SPRITE_WIDTH)) {
            
            character->x++;
            return 0;
        }
    }

    
    if (currentBackground == 1) {
        if (xscroll2 < MAX_SCROLL) {
            character->x++;
            return 1;
        } else if (character->x < (SCREEN_WIDTH - CHARACTER_SPRITE_WIDTH)) {
            character->x++;
            return 0;
        }
    }

    
    return 0;
}

/* stop the koopa from walking left/right */
void character_stop(struct Character* character) {
    character->move = 0;
    character->frame = 0;
    character->counter = 7;
    sprite_set_offset(character->sprite, character->frame);
}

/* start the koopa jumping, unless already fgalling */
void character_jump(struct Character* character) {
    if (!character->falling) {
        character->yvel = -1350;
        character->falling = 1;
        character->jumps--;
    }
}

/* finds which tile a screen coordinate maps to, taking scroll into acco  unt */
unsigned short tile_lookup(int x, int y, int xscroll, int yscroll,
        const unsigned short* tilemap, int tilemap_w, int tilemap_h) {

    /* adjust for the scroll */
    x += xscroll;
    y += yscroll;

    /* convert from screen coordinates to tile coordinates */
    x >>= 3;
    y >>= 3;

    /* account for wraparound */
    while (x >= tilemap_w) {
        x -= tilemap_w;
    }
    while (y >= tilemap_h) {
        y -= tilemap_h;
    }
    while (x < 0) {
        x += tilemap_w;
    }
    while (y < 0) {
        y += tilemap_h;
    }

    /* the larger screen maps (bigger than 32x32) are made of multiple stitched
       together - the offset is used for finding which screen block we are in
       for these cases */
    int offset = 0;

    /* if the width is 64, add 0x400 offset to get to tile maps on right   */
    if (tilemap_w == 64 && x >= 32) {
        x -= 32;
        offset += 0x400;
    }

    /* if height is 64 and were down there */
    if (tilemap_h == 64 && y >= 32) {
        y -= 32;

        /* if width is also 64 add 0x800, else just 0x400 */
        if (tilemap_w == 64) {
            offset += 0x800;
        } else {
            offset += 0x400;
        }
    }

    /* find the index in this tile map */
    int index = y * 32 + x;

    /* return the tile */
    return tilemap[index + offset];
}


/* update the koopa */
/* update the character */
void character_update(struct Character* character, int xscroll) {
    /* update y position and speed if falling */
    if (character->falling) {
        character->y += (character->yvel >> 8);
        character->yvel += character->gravity;
    }

    /* determine the bottom of the character sprite for collision detection */
    int character_bottom_y = character->y + CHARACTER_SPRITE_HEIGHT;
    /* check which tile the character's bottom is over */
    unsigned short tile = tile_lookup(character->x + 8, character_bottom_y + 10, xscroll, 0, background,
                                      background_width, background_height);

    /* if it's a solid tile */
    if (tile == 0) {        /* stop the fall! */
        character->falling = 0;
        character->yvel = 0;

        /* align the character's bottom with the top of the tile */
                character->y = ((character_bottom_y >> 3) << 3) - CHARACTER_SPRITE_HEIGHT;
    } else {
        /* the character is falling */
        character->falling = 1;
    }

    /* update animation if moving */
    if (character->move) {
        character->counter++;
        if (character->counter >= character->animation_delay) {
            character->frame = character->frame + 16;
            if (character->frame > 16) {
                character->frame = 0;
            }
            sprite_set_offset(character->sprite, character->frame);
            character->counter = 0;
        }
    }

    /* set on-screen position */
    sprite_position(character->sprite, character->x, character->y);
}


/* the main function */
int main() {
    /* we set the mode to mode 0 with bg0 on */
    *display_control = MODE0 | BG0_ENABLE | SPRITE_ENABLE | SPRITE_MAP_1D;

    /* setup the background 0 */
    setup_background();

    /* setup the sprite image data */
    setup_sprite_image();

    /* clear all the sprites on screen now */
    sprite_clear();

    /* create the koopa */
    struct Character character;
    character_init(&character);

    /* loop forever */
    while (character.jumps >= 0) {
        /* Character movement */
        if (button_pressed(BUTTON_RIGHT)) {
            if (character_right(&character)) {
                if (currentBackground == 0 && xscroll1 < MAX_SCROLL) {
                    xscroll1++;
                } else if (currentBackground == 1) {
                    xscroll2++;
                }
            }
        } else if (button_pressed(BUTTON_LEFT)) {
            if (character_left(&character)) {
                if (currentBackground == 0 && xscroll1 > 0) {
                    xscroll1--;
                } else if (currentBackground == 1 && xscroll2 > 0) {
                    xscroll2--;
                }
            }
        } else {
            character_stop(&character);
        }

        /* Check for jumping */
        if (button_pressed(BUTTON_A)) {
            character_jump(&character);
        }

        /* Background transition logic */
        if (currentBackground == 0 && character.x >= (SCREEN_WIDTH - CHARACTER_SPRITE_WIDTH - character.border)) {
            // Transition to the second background
            currentBackground = 1;
            setup_background2();

            // Reset character position at the left of the screen and scroll for second background
            character.x = character.border;
            xscroll2 = 0;
        } else if (currentBackground == 1 && character.x < character.border) {
            // Transition back to the first background
            currentBackground = 0;
            setup_background();

            // Position character near the right edge and reset scroll for first background
            character.x = SCREEN_WIDTH - CHARACTER_SPRITE_WIDTH - character.border;
            xscroll1 = MAX_SCROLL;
        }

        /* Update character and scroll */
        character_update(&character, currentBackground == 0 ? xscroll1 : xscroll2);

        /* wait for vblank before scrolling and moving sprites */
        wait_vblank();
        *bg0_x_scroll = currentBackground == 0 ? xscroll1 : xscroll2;
        sprite_update_all();

        /* delay some */
        delay(300);
    }
    
    /*end screen area*/
    while(1) {
    }
}
