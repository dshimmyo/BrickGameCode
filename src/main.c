#include <stdint.h>
#include "gt/gametank.h"
#include "gt/gfx/draw_queue.h"
#include "gen/assets/audio.h"
#include "gt/audio/music.h"
// #include "gt/input.h"
#include "gt/feature/paddleInput/paddleInput.h" 
#include "gt/feature/random/random.h"
#include "scoring.h"
#include "gen/assets/gfx.h"
#include "gt/banking.h" //Banking API
#include "gen/bank_nums.h" //Generated macros for named bank numbers
#include "gt/feature/text/text.h"
#include "gen/assets/font.h"
#include "gen/assets/sdk_default.h"
#include "gt/feature/persist/persist.h"

extern int speed_mult_asm(int source, int mult);
extern int approx_five_twelfths(int num);

#define VERSION_MAJOR 1 //only single digit haha never change!
#define VERSION_MINOR 1 //now up to 99
#define VERSION_PATCH 3 //now up to 99

//version logs for some reason:
//v 1 0 0 first rc
//v 1 0 1 reduced anim queue from 6 to 4, more color depth in animations
//v 1 0 2 emulator switches a,b to z,x when no joystick detected
//v 1 0 3 recognize paddle bit and mouse bit from emu
//v 1 0 4 fix paddle trans, disable test features
//v 1 0 5 demo unlocks with dks paddle connected
//v 1 0 6 4px font fixes
//v 1 0 7 demomode attract mode fix
//v 1 1 0 remove/disable persist feature
// // v 1 1 1 implemented persistent hiscore, but only for liretro
//v 1 1 2 adds 2 digit version, moves level number up by one pixel, gameover spam out

//v 1 1 3 re-instating persist and hiscore and making it work with steam cloud

/////*********** 
#define TESTFEATURES false
#define DEMOMODE false
#if DEMOMODE
bool demoModeUnlock = false;
#endif
unsigned char demoModeFrames = 0;//need to update a couple frames
/////*********** 

#define CHK_EMU *((volatile uint8_t*) 0x2007) //emulator magic ID
#define GAMEPAD_1_REG (*(volatile unsigned char*)0x2008) //using read-only memory to send messages to the emulator
#define GAMEPAD_2_REG (*(volatile unsigned char*)0x2009) //using read-only memory to send messages to the emulator
#define RUMBLE_PORT GAMEPAD_1_REG //magic rumble port
#define EMULATOR_INPUT_TYPE GAMEPAD_2_REG //tell emulator paddle or gamepad
#define SIGNAL_PADDLE_MODE 0xA5 // Use a distinct signature byte for paddle mode

//hiscore stuff for emu/steam
#define HYPER_HISCORE_BYTE0  (*(volatile uint8_t*)0x2400)
#define HYPER_HISCORE_BYTE1  (*(volatile uint8_t*)0x2401)
#define HYPER_HISCORE_BYTE2  (*(volatile uint8_t*)0x2402)
#define HYPER_HISCORE_BYTE3  (*(volatile uint8_t*)0x2403)
#define HYPER_COMMIT_SAVE    (*(volatile uint8_t*)0x2404)
#define HYPER_SAVE_STATUS    (*(volatile uint8_t*)0x2405)

void Set_emulator_paddle_mode(bool value) {
    // We explicitly write to the register.
    // Real hardware ignores this; your emulator traps it.
    if (value) GAMEPAD_2_REG = SIGNAL_PADDLE_MODE;
    else GAMEPAD_2_REG = 0x00;
}

//CHK_EMU 
//CHK_EMU & 0x02 indicates that a joystick is connected and the game should setrange the input
//0x08 is mouse emulation
//0x20 is dksPaddle
#define CHECK_PADDLE_STATE() \
    if ((CHK_EMU & 0x02) && !(CHK_EMU & 0x08)) { \
        paddleRotationMin = PADDLEMIN; \
        paddleRotationMax = PADDLEMAX; \
    } else { \
        paddleRotationMin = MOUSEMIN; \
        paddleRotationMax = MOUSEMAX; \
    }

#define BGCOLOR 0b11100000    //was 2
#define PADDLECOLOR 0b11010111//0b00111111//0b10101111
#define STARTPADDLEWIDTH 24//16
#define PADDLEHEIGHT 4
#define PADDLEY 108
#define BALLSIZE 2
#define BALLCENTEROFFSET ((BALLSIZE>2) ? BALLSIZE>>1 : 0)
#define BINARYTESTPOSY 10

#define BAKEDBACKGROUNDSPRITEBANK 1
#define BRICKSSPRITEBANK 2//3
#define PRESSSTARTSPRITEBANK 3//4
#define PADDLESPRITEBANK 4//5
#define TITLESPRITEBANK 5

#define LOADSPRITEBG false

#define BRICK_HEIGHT 4
#define MAX_BRICK_COLS 120
#define NUM_BRICK_ROWS 6

#define CEILING 21

#define BRICK_TOP    35//39//25 + 18
#define BRICK_BOTTOM 58//58//44 + 18
#define BRICK_LEFT   4//6//4//4 pixels border but maybe only 3 visible
#define BRICK_RIGHT  123//123//4 pixels border
#define BRICK_ANIMATION true

#define MAX_NOISE 64

#define CONTROLLER_STYLE_PADDLE0 0
#define CONTROLLER_STYLE_GPAD1 1
#define MAX_BALLS 4 //planning for insane mode

#define ERASE_QUEUE_MAX 8 //eight is enough
#define NUM_ANIM_FRAMES 16 //24 too much//8 // 4 look unimpressive //6 pushing it //8 a little sluggish
#define ANIM_QUEUE_MAX 4 // was 2 but back to 4 after math optimizations//4 - is nice but trying 2 combined with 6 frames as a compromise
#define ANIM_FRAME_MULTIPLIER 1//2
#define ATTRACT_MODE_SKIP_FRAME false

//persist hiscore implementation
typedef struct {
    char canary;
    uint32_t hiscore;
} SaveData;

// Pointer to the start of the save area in Flash ROM
#define FLASH_SAVE_ADDR ((SaveData*) &save_canary)

#define MAGIC_SAVE_NUM 0x42

extern const char save_canary;
extern const uint32_t saved_hiscore;


//dynamic brick layout
unsigned char currentBrickWidth = 120;//12;//8;//4;//2;
unsigned int currentNumBricksH = 1;//10;//15;//30;//60;
unsigned char currentPaddleWidth = STARTPADDLEWIDTH;
unsigned char minPaddleWidth = 4;
unsigned char paddleShrinkAmount = 4;

//char gamestate = 0;
typedef enum {
STATE_CONTROLLER_SELECT,
STATE_BREAKOUT_MENU,
STATE_BREAKOUT_READY,
STATE_ATTRACT,
STATE_BREAKOUT_PLAY,
STATE_BREAKOUT_GAMEOVER
} GameState;
GameState gameState;

typedef enum {
    SUBSTATE_NEW_LEVEL,
    SUBSTATE_NEW_BALL,
    SUBSTATE_PLAY
} GameplaySubState;
GameplaySubState subState;
#define MOUSEMIN 0
#define MOUSEMAX 255
#define PADDLEMIN 64
#define PADDLEMAX 192
bool isEmulator = false;
unsigned char paddleRotationMin=64;//64
unsigned char paddleRotationMax=192;//192

char controller_style = 0;
unsigned char lives_left = 3;
char breakoutRefresh=0;//set to true to refresh the whole screen
char breakoutBricksRefresh=0;//set to true to refresh the whole screen
unsigned int bricks_remaining = 0;
char messageEraseCount = 0;//set to 2 or 3 to erase messages for 2 or 3 frames

//typedefs
typedef struct {
    int dx;//int allows for more speed increase
    int dy;
} angle;

typedef struct {
    bool visible[MAX_BRICK_COLS];//allocate max number of bricks
    char erase_frames[MAX_BRICK_COLS];
} EntityRow;

// A small structure to track pending erases
typedef struct {
    //int brick_index;//no longer used
    uint8_t frames_left;
    unsigned char x;
    unsigned char y;
    unsigned char w;
    unsigned char h;
} EraseTask;

typedef struct {
    char numColumns;//allocate max number of bricks
    char brickWidth;
} BrickLayout;

typedef struct { 
    int x, y; //signed just in case, to avoid overflow when negative
    int x_old, y_old;         // Last frame's position
    int x_old_old, y_old_old; // Position from 2 frames ago (for double-buffering)
    int dx, dy; // Normalized direction (-1, 0, 1) 
    int dxRem, dyRem; // sub-pixel remainders
    int speed; // 1 = 5/12, 2 = 10/12, etc. 
    bool active; 
    bool was_active_old; 
    bool was_active_old_old; 
} Ball;

uint16_t magicBrickIndex=0;
// extern uint16_t get_brick_index_asm(uint8_t x, uint8_t y);//buggy

//prototypes
void check_brick_collision_prog1(char *_ball_x, char *_ball_y, int *_ball_dx, int *_ball_dy, int *_ballSpeed);
void BrickHeal_prog1(unsigned char, unsigned char, unsigned char);
void process_anim_queue_v2_prog1();
void BreakoutBricksSetupWrapper_prog1(char);
void render_paddle_prog1();
void spawn_new_ball_prog1(int , int , int , int , unsigned char speed);
void spawn_new_rnd_ball_prog1();
void erase_paddle_prog1();
void erase_balls_prog1();
void process_all_balls_prog1();
void render_balls_prog1();
angle get_random_spawn_angles_prog1();
void init_ball_system_prog1();
unsigned char get_active_ball_count_prog1();
void EraseRect(char x, char y, char w, char h);
void draw_lives_prog1(char);
uint16_t get_brick_index_prog1(uint8_t, uint8_t);
void printBlinkMessage_prog1(char index);
void loading_screen_prog1();
void reset_reflect_angles_prog1();
void increase_ball_speed_prog1(unsigned char level);
void generate_paddle_sprite_prog1();
void bake_wave_number_prog1(unsigned char wave, int posx, int posy, char bank);


#define NUM_ANGLES 22
#pragma rodata-name (push, "PROG1")
const angle reflectAnglesNew_prog1[NUM_ANGLES]=
{{-251<<1,-30<<1},{-247<<1,-61<<1},{-235<<1,-86<<1},{-224<<1,-112<<1},{-202<<1,-146<<1},{-181<<1,-181<<1},{-146<<1,-202<<1},{-112<<1,-224<<1},{-86<<1,-235<<1},{-61<<1,-247<<1},{-30<<1,-251<<1},
{30<<1,-251<<1},{61<<1,-247<<1},{86<<1,-235<<1},{112<<1,-224<<1},{146<<1,-202<<1},{181<<1,-181<<1},{202<<1,-146<<1},{224<<1,-112<<1},{235<<1,-86<<1},{247<<1,-61<<1},{251<<1,-30<<1}};
angle currentReflectAngles_prog1[NUM_ANGLES];//bake speed increase
#pragma rodata-name (pop)

unsigned char brickRowPoints[6]={4,3,2,2,1,1};//number of points gained by hitting a brick in each row, top to bottom

EntityRow brickRows[NUM_BRICK_ROWS];//init in NewLevel()

EraseTask erase_queue[ERASE_QUEUE_MAX];
unsigned char erase_queue_count = 0;

EraseTask anim_queue[ANIM_QUEUE_MAX]; //10 frames of animation over maybe double frames for up to two balls animframes * 2X * maxballs
unsigned char  anim_queue_count = 0;

//unsigned char animatedBricks[MAX_BRICK_COLS*NUM_BRICK_ROWS]; //this needs to be re-engineered

#define NUM_BRICK_LAYOUTS 11//16
BrickLayout brickLayouts[NUM_BRICK_LAYOUTS]=
{
    //{cols,width}
    //{1,120},
    //{2,60},
    //{3,40},
    {4,30},
    //{5,24},
    //{6,20},
    {8,15},
    {10,12},
    {12,10},
    {15,8},
    {20,6},
    {24,5},
    {30,4},
    {40,3},
    {60,2},
    {120,1}
};

//new ball variables:
unsigned char active_balls_count = 0;
Ball balls[MAX_BALLS];//pre-allocate enough space for MAX_BALLS

char paddleX = 64;//global paddle xpos
char paddleX_old=64;
char paddleX_old_old=64;
unsigned char button_byte=0;//8-bit byte representing button states, for debugging and potential use in game mechanics
unsigned int score = 0;//global score variable
uint32_t hiscore = 100;//global score variable
unsigned char scoreRefreshFrames = 2;
unsigned char hiscoreRefreshFrames = 2;
unsigned char livesRefreshFrames = 2;
int newLevelTimer = 100;//delay between brick reset and ball reset
int gameOverTimer = 100;//delay between brick reset and ball reset
int spawnTimer = 100;
int rndPaddleOffset = 0;
unsigned char rndPaddleOffsetTimer = 0;

int blinkTimer=0;
char blinkMessageVisible=0;
unsigned int readyTimer=0;
unsigned char cycleLevel=0;
bool isBallWentOutOfBounds = false;

void load_hiscore() {
    if (isEmulator){
        // Check if the host wrapper has a valid save file on disk
        if (HYPER_SAVE_STATUS == 0x01) {
            uint32_t b0 = (uint32_t)HYPER_HISCORE_BYTE0;
            uint32_t b1 = (uint32_t)HYPER_HISCORE_BYTE1;
            uint32_t b2 = (uint32_t)HYPER_HISCORE_BYTE2;
            uint32_t b3 = (uint32_t)HYPER_HISCORE_BYTE3;

            hiscore = b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
        } else {
            hiscore = 0; // Default if no save exists
        }
    }
    else
    {
        push_rom_bank();
        change_rom_bank(BANK_SAVE);

        // Check if valid data has been written to Flash
        if (FLASH_SAVE_ADDR->canary == MAGIC_SAVE_NUM) {
            hiscore = FLASH_SAVE_ADDR->hiscore;
        } else {
            // Sector is uninitialized (0xFF) or invalid
            hiscore = 0;
        }

        pop_rom_bank();
    }
}

void save_hiscore(uint32_t new_score) {
    SaveData data_to_save;
    
    hiscore = new_score;
    if (isEmulator){
        //emulator should use a different save method targeting mainly wrapper mode and steam cloud
        HYPER_HISCORE_BYTE0 = (uint8_t)(new_score & 0xFF);
        HYPER_HISCORE_BYTE1 = (uint8_t)((new_score >> 8) & 0xFF);
        HYPER_HISCORE_BYTE2 = (uint8_t)((new_score >> 16) & 0xFF);
        HYPER_HISCORE_BYTE3 = (uint8_t)((new_score >> 24) & 0xFF);

        // Writing 0x01 tells host C++ code to execute SaveHighScoreToNativeFile()
        HYPER_COMMIT_SAVE = 0x01;
    }
    else
    {
        data_to_save.canary = MAGIC_SAVE_NUM;
        data_to_save.hiscore = new_score;

        // Erase the Flash sector before writing new values
        clear_save_sector();

        // Write structure from RAM to Flash ROM
        save_write(&data_to_save, (void*) FLASH_SAVE_ADDR, sizeof(SaveData));
    }
}

void trigger_rumble(unsigned char strength) {
    // Writing here does nothing on real hardware (it's open bus)
    // But the Emulator is 'watching' this specific address.
    RUMBLE_PORT = strength; 
}
void playBass(char row){

    switch (row){
        case 0:
        play_sound_effect(ASSET__audio__row0_sfx_ID,127);
        break;
        case 1:
        play_sound_effect(ASSET__audio__row1_sfx_ID,127);
        break;
        case 2:
        play_sound_effect(ASSET__audio__row2_sfx_ID,127);
        break;
        case 3:
        play_sound_effect(ASSET__audio__row3_sfx_ID,127);
        break;
        case 4:
        play_sound_effect(ASSET__audio__row4_sfx_ID,127);
        break;
        case 5:
        play_sound_effect(ASSET__audio__row5_sfx_ID,127);
        break;
        default:
        play_sound_effect(ASSET__audio__row5_sfx_ID,127);
        break;
    }
    if (isEmulator){trigger_rumble(128);}
}

void soundTest(){
    play_sound_effect(ASSET__audio__flongNew_sfx_ID,1);
}
// void soundOutOfBounds()
// {
//     play_sound_effect(ASSET__audio__outOfBounds_sfx_ID,1);
// }
void soundTestA(){
    play_sound_effect(ASSET__audio__chirp_sfx_ID,1);
}
void soundCol(){
    play_sound_effect(ASSET__audio__bik_sfx_ID,1);
}
void soundWall(){
    play_sound_effect(ASSET__audio__wall_sfx_ID,1);
}
void soundCeiling(){
    play_sound_effect(ASSET__audio__ceiling_sfx_ID,(char)1);
}

#pragma code-name (push, "PROG1")

void shrink_paddle_prog1() {
    if (currentPaddleWidth > minPaddleWidth) {
        currentPaddleWidth = (currentPaddleWidth>minPaddleWidth) ? (currentPaddleWidth - paddleShrinkAmount) : currentPaddleWidth;
        if (isEmulator) trigger_rumble(100); // A mid-strength "notification" buzz
    }
}
#define GET_BRICK_INDEX_PROG1(x, y) ( \
    (uint16_t)((uint8_t)((y) - (BRICK_TOP)) / (BRICK_HEIGHT)) * (currentNumBricksH) + \
    (uint8_t)((x) - (BRICK_LEFT)) / (currentBrickWidth) \
)
// uint16_t get_brick_index_prog1(uint8_t x, uint8_t y){
//     uint8_t row = (y - BRICK_TOP)/BRICK_HEIGHT;
//     uint8_t col = (x - BRICK_LEFT)/currentBrickWidth;
//     return row*currentNumBricksH + col;
// }
unsigned char get_active_ball_count_prog1() {//this makes it work better but it should be removed
    char i;
    active_balls_count=0;
    for (i = 0; i < MAX_BALLS; i++) {
        if (balls[i].active) {
            active_balls_count++;
        }
    }
    return active_balls_count;
}
#pragma code-name (pop)

#pragma code-name (push, "PROG1")
unsigned char GetNearestReflectAngleIndex_prog1( int _dx)
{
    unsigned char index=0;
    unsigned char diff = 255;
    unsigned char newDiff;
    char bestIndex=0;
    for (index=0; index<NUM_ANGLES; index++){
        if (currentReflectAngles_prog1[index].dx == _dx) return index;
        if (_dx > currentReflectAngles_prog1[index].dx) newDiff = _dx - currentReflectAngles_prog1[index].dx;
        else newDiff = currentReflectAngles_prog1[index].dx - _dx;
        if (newDiff < diff){
            diff = newDiff;
            bestIndex = index;
        }
    }
    return bestIndex;
}
#pragma code-name (pop)

#pragma code-name (push, "PROG1")
bool detectPaddleCollision_prog1(char sourceXLow,char sourceXHi,char sourceYLow,char sourceYHi,
    int *_dx, int *_dy, int *_ballSpeed)
{
    unsigned char zoneWidth = currentPaddleWidth >> 3; 
    const char angleAdjust = 32;//64
    const char minAngle = 96;//64
    const char maxAngle = 512 - minAngle;
    char paddleHitRegion = 0;
    char nearestIndex=0;
    angle nearestAngle;
    int tempDx = *_dx;//keep changes local until done
    int tempDy = *_dy;//keep changes local until done
    if (zoneWidth == 0) zoneWidth = 1; // 4px paddle safety

    if (tempDy < 0) return false;//don't waste time on already bounced balls
    if (sourceXLow > paddleX + currentPaddleWidth) return false;

    if (sourceXLow > paddleX) paddleHitRegion = (sourceXLow - paddleX)/zoneWidth;
    else paddleHitRegion = 0;
    if (paddleHitRegion > 7) paddleHitRegion = 7;//there are only 8 zones
    
    if (sourceYHi < PADDLEY) return false;
    else if (sourceYLow > PADDLEY+PADDLEHEIGHT) return false;
    else if (sourceXHi < paddleX) return false;

    //if it gets to here the ball is colliding with the paddle, now determine reflection angle based on hit region

    //if the ball is in slow mode, turn it up
    *_ballSpeed = (*_ballSpeed<2) ? 2 : *_ballSpeed;

    //attempt to make sophisticated reflections:
    //nearestIndex = GetNearestReflectAngleIndex(tempDx);

    //22 angles
    //0  1  2  3  4  5  6  7  8  9  10
    //21 20 19 18 17 16 15 14 13 12 11
    if (currentPaddleWidth == 4){
        switch (paddleHitRegion)
        {
            case 0: 
            case 1:
            nearestIndex = 4;//1//0//A send ball far left
            //nearestIndex = 6;
            break;

            case 2: 
            case 3: 
            nearestIndex = 8;//BC slight left
            //nearestIndex = 10;//E almost vertical left
            break;

            case 4: 
            case 5:
            //nearestIndex = 11;//E almost vertical right
            nearestIndex= 13;//FG nudges right 
            break;

            case 6:
            case 7:
            //nearestIndex = 15;//nudges right
            nearestIndex = 17;//20;//21//far right
            break;
        }
    }
    else
    {
        switch (paddleHitRegion)
        {
            case 0: //A send ball far left
            nearestIndex = 4;//1//0
            break;
            case 1:
            nearestIndex = 6;
            break;
            case 2: //BC slight left
            nearestIndex = 8;
            break;
            case 3: //E almost vertical left
            nearestIndex = 10;
            break;
            case 4: //E almost vertical right
            nearestIndex = 11;
            break;
            case 5://FG nudges right 
            nearestIndex= 13;
            break;
            case 6://nudges right
            nearestIndex = 15;
            break; 
            case 7://H far right
            //case 8://extra zone? far right
            nearestIndex = 17;//20;//21
            break;
        }
    }
    nearestAngle = currentReflectAngles_prog1[nearestIndex];
    *_dx = nearestAngle.dx;
    *_dy = nearestAngle.dy;
    play_sound_effect(ASSET__audio__paddle_sfx_ID,(char)0);
    if (isEmulator){trigger_rumble(255);}

    return true;
}
angle get_random_spawn_angles_prog1()
{
    char returnIndex;
    int indexIndex = rnd_range(0,7);
    angle returnAngle;
    switch (indexIndex)
    {
        case 0: //A send ball far left
        returnIndex = 4;//1//0
        break;
        case 1:
        returnIndex = 6;
        break;
        case 2: //BC slight left
        returnIndex = 8;
        break;
        case 3: //E almost vertical left
        returnIndex = 10;
        break;
        case 4: //E almost vertical right
        returnIndex = 11;
        break;
        case 5://FG nudges right 
        returnIndex= 13;
        break;
        case 6://nudges right
        returnIndex = 15;
        break; 
        case 7://H far right
        //case 8://extra zone? far right
        returnIndex = 17;//20;//21
        break;
    }
    returnAngle = currentReflectAngles_prog1[returnIndex];
    return returnAngle;
}
#pragma code-name (pop)


#pragma code-name (push, "PROG1")

// int approx_five_twelfths(int num){
//     return (num * 13) >> 5;
// }
// int speedMult_prog1(int source,int mult){//multiplier can be fine-tuned here and hidden from code
//     return approx_five_twelfths(source) * mult;
// }
/*
void boxMotion_prog1()
{
    //check collision for every frame
    //frameskipping/subframe should only limit incrementing movement
    //boxes draw left-right top-down 0->127, 7->120
    int scaledDx = speedMult_prog1(dx,ballSpeed);//dx * ballSpeed / 2;
    int scaledDy = speedMult_prog1(dy,ballSpeed);//dy * ballSpeed / 2;
    int dxTot = scaledDx + dxRem;
    int dyTot = scaledDy + dyRem;
    if (((unsigned int) dxTot >= 255 || (unsigned int) dyTot >= 255))
    {
        box_x += dxTot>>8;
        box_y += dyTot>>8;

        if(box_y <= CEILING+1) {//7
            dy = (dy<0) ? -dy : dy;
            soundCeiling();
        } else if(box_y >= 120-BALLSIZE){//112) {
            randomizeBox_prog1(&box_x, &box_y, &dx, &dy, &ballSpeed);
            soundTest();
        }

        if(box_x <= BRICK_LEFT) {
            dx = (dx<0) ? -dx : dx;
            soundWall();
        } else if(box_x >= BRICK_RIGHT-BALLSIZE+1 ) {
            dx = (dx>0) ? -dx : dx;
            soundWall();
        } else if (detectPaddleCollision_prog1(box_x,box_x+BALLSIZE,box_y, box_y+BALLSIZE,&dx,&dy,&ballSpeed)){
            //dy = (dy>0) ? -dy : dy;
            //box_y = PADDLEY-BALLSIZE-1;//height correction, maybe redundant
            dyTot=0;//neutralize accumulated y motion
            dyRem=0;
            //soundCol();
        }
        check_brick_collision_prog1(&box_x,&box_y,&dx,&dy,&ballSpeed);
    }
    dxRem = dxTot & 255;// % 256;//update the remainder for sub-frame movement
    dyRem = dyTot & 255;//% 256;//update the remainder for sub-frame movement
}*/
#pragma code-name (pop)

#pragma code-name (push, "PROG1")
void update_ball_logic_prog1(Ball *b) 
{
    // Use your existing 5/12ths scalar logic (speedMult)
    int scaledDx = speed_mult_asm(b->dx, b->speed);
    int scaledDy = speed_mult_asm(b->dy, b->speed);
    
    // Accumulate movement with the stored remainder
    int dxTot = scaledDx + b->dxRem;
    int dyTot = scaledDy + b->dyRem;

    // 2. Update History (Crucial for the Erase/Heal cycle)
    b->x_old_old = b->x_old;
    b->y_old_old = b->y_old;
    b->x_old = b->x;
    b->y_old = b->y;
    b->was_active_old_old = b->was_active_old;
    b->was_active_old = b->active;

    if (!b->active) return;
    // Check if we've accumulated enough for a whole pixel move (>= 256)
    if (((unsigned int)dxTot >= 255 || (unsigned int)dyTot >= 255)) 
    {
        b->x += dxTot >> 8;
        b->y += dyTot >> 8;


        // --- BRICKS ---
        // Pass the ball's specific variables to your collision detector
        // order-of-operations, check brick collisions before wall collisions so we
        // don't risk missing a thin brick

        check_brick_collision_prog1(&b->x, &b->y, &b->dx, &b->dy, &b->speed);

        // --- CEILING / OUT OF BOUNDS ---
        if(b->y <= CEILING + 1) {
            b->dy = (b->dy < 0) ? -b->dy : b->dy;
            soundCeiling();
        } 
        else if(b->y >= 127) {//120 - BALLSIZE
            b->active = false;//despawn, stops rendering
            soundTest();
            isBallWentOutOfBounds = true;
        }

        // --- WALLS ---
        if(b->x <= BRICK_LEFT) 
        {
            b->dx = (b->dx < 0) ? -b->dx : b->dx;
            soundWall();
        } 
        else if(b->x >= BRICK_RIGHT )//was BRICK_RIGHT - BALLSIZE + 1 
        {
            b->dx = (b->dx > 0) ? -b->dx : b->dx;
            //b->x = BRICK_RIGHT-1;//fix intersection, "snap" ball, not sure I like this
            soundWall();
        } 
        
        // --- PADDLE ---
        if (detectPaddleCollision_prog1(b->x, b->x + BALLSIZE, b->y, b->y + BALLSIZE, &b->dx, &b->dy, &b->speed)) 
        {
        // The pointers inside the function will now update the 
        // specific ball's dx, dy, and speed in the array.
        b->dyRem = 0; 
        }

        
    }

    // Update the remainder for the next frame
    b->dxRem = dxTot & 255;
    b->dyRem = dyTot & 255;
}
#pragma code-name (pop)


int dxARem=0;//remainder
int dyARem=0;

#pragma code-name (push, "PROG1")
/*
void boxAMotion_prog1()
{
    int scaledDx = speedMult_prog1(dxA,ballSpeedA);//dxA * ballSpeedA / 2;
    int scaledDy = speedMult_prog1(dyA,ballSpeedA);//dyA * ballSpeedA / 2;
    int dxATot = scaledDx + dxARem;
    int dyATot = scaledDy + dyARem;
    if (((unsigned int) dxATot >= 255 || (unsigned int) dyATot >= 255))
    {
        boxA_x += dxATot>>8;
        boxA_y += dyATot>>8;

        if(boxA_y <= CEILING+1) {//7
            dyA = (dyA<0) ? -dyA : dyA;
            soundCeiling();
        } else if(boxA_y >= 120-BALLSIZE) {
            randomizeBox_prog1(&boxA_x, &boxA_y, &dxA, &dyA, &ballSpeedA);
            soundTest();
        }

        if (boxA_x <= BRICK_LEFT) {
            dxA = (dxA<0) ? -dxA : dxA;
            soundWall();
        } else if(boxA_x >= BRICK_RIGHT-BALLSIZE+1) {
            dxA = (dxA>0) ? -dxA : dxA;
            soundWall();
        } else if (detectPaddleCollision_prog1(boxA_x,boxA_x+BALLSIZE,boxA_y, boxA_y+BALLSIZE,&dxA,&dyA,&ballSpeedA)){
            //dyA = (dyA>0) ? -dyA : dyA;
            //boxA_y = PADDLEY-BALLSIZE-1;//height correction, maybe redundant
            dyATot = 0; //neutralize accumulated y motion
            dyARem =0;
            //soundCol();
        }

        check_brick_collision_prog1(&boxA_x,&boxA_y,&dxA,&dyA,&ballSpeedA);
    }
    dxARem = dxATot & 255;//% 256;//update the remainder for sub-frame movement
    dyARem = dyATot & 255;//% 256;//update the remainder for sub-frame movement
}*/
#pragma code-name (pop)

#pragma code-name (push, "PROG2")
void update_paddle_offset_prog2(){
    if (rndPaddleOffsetTimer++ > 128)
    {
        rndPaddleOffset = rnd_range(0,currentPaddleWidth>>1);
        if (rnd_range(0,127)<64) rndPaddleOffset *= -1; 
        rndPaddleOffsetTimer=0;
    }
}

#pragma code-name (pop)

static int accumulator = 0;

#pragma code-name (push, "PROG1")

char ClampPaddleX_prog1(int paddlex){
    if (paddlex < 0){
        paddlex = 0;
    } else if (paddlex>128-(currentPaddleWidth>>1)){
        paddlex = 128 - (currentPaddleWidth>>1);
    }
    return paddlex;
}

int setRangeOpt_prog1(unsigned char input, unsigned char inMin, unsigned char inMax)
{
    int output = 0;
    int target = (int)input << 8; // 1. Convert 8-bit input to 16-bit for "Internal" math
    unsigned char smoothedInput;
    // sub-pixel nudge
    // We move the accumulator toward the target by a fraction
    // This averages out the 1-bit jitter over a couple of frames
    accumulator += (target - accumulator) >> 1;

    // extract the smoothed 8-bit value for the range math
    smoothedInput = (unsigned char)(accumulator >> 8);

    if (smoothedInput < inMin) return 0;
    else if (smoothedInput > inMax) return 127;

    output = (((127) << 8) / (inMax - inMin) * (smoothedInput - inMin) >> 8);

    return output;
}

char paddleXFromPot8opt_prog1(unsigned char potVal)
{
    //need to make a cheaper setrange function
    unsigned char newPotVal = setRangeOpt_prog1(potVal,paddleRotationMin,paddleRotationMax);//changed for 8-bit

    return ClampPaddleX_prog1(newPotVal);//(((newPotVal<<1)/3 + paddleX/3));//no smoothing
}
signed int dpadDx = 0;
static signed int paddleSubX = 0;
#define MAX_DPAD_PADDLE_SPEED 3 * 64
char paddleXFromDPad_prog1(unsigned char level)
{
    signed int result = paddleX;

    //dpadDx old behavior -3, 0, 3
    //new behavior signed int try -256 * 3, 0, 256*3
    if (!((player1_buttons & INPUT_MASK_PADDLE_RIGHT) || (player1_buttons & INPUT_MASK_PADDLE_LEFT)))
    {
        dpadDx=0;//if no press, start back at 0
        paddleSubX = 0; // Clear fractional accumulation when stopped
        return result;
    }
    else
    {
        int maxSpeed = MAX_DPAD_PADDLE_SPEED * (50 + level ) / 50;//64 scale

        if ((player1_buttons & INPUT_MASK_PADDLE_RIGHT)){
            dpadDx = (dpadDx<=0) ? 0 : dpadDx;
            dpadDx = (dpadDx<maxSpeed) ? (dpadDx+32) : dpadDx;
        }
        else if ((player1_buttons & INPUT_MASK_PADDLE_LEFT)) {
            dpadDx = (dpadDx>=0) ? 0 : dpadDx;
            dpadDx = (dpadDx>(maxSpeed*-1)) ? (dpadDx-32) : dpadDx;
        }
        paddleSubX += dpadDx;
        result += (paddleSubX / 64);
        paddleSubX = paddleSubX % 64;
        return ClampPaddleX_prog1(result);
    }
}
char ConstVelocity_prog1(char source, int target, char vel){
    int result = source;
    if (source < target){
        result = source + vel;
        if (result > target) result = target;
    }
    else if (source > target){
        result = source - vel;
        if (result < target) result = target;
    }
    return ClampPaddleX_prog1(result);
}
unsigned char everyOtherFrame;

char paddleXFromLowestBall_prog1(){
    unsigned char lowest_y = 0;
    signed char target_ball_index = -1;
    unsigned char i;
    int maxSpeed = (MAX_DPAD_PADDLE_SPEED * (50 + cycleLevel ) / 50)/64;//64 scale

#if ATTRACT_MODE_SKIP_FRAME
    maxSpeed *= 2;
    if ((everyOtherFrame++) & 0x00000001) return paddleX;//skip every other frame
#endif

    // Find the ball closest to the paddle
    for (i = 0; i < MAX_BALLS; i++) {
        if (balls[i].active && balls[i].y > lowest_y) {
            lowest_y = balls[i].y;
            target_ball_index = i;
        }
    }

    return ConstVelocity_prog1(paddleX, balls[target_ball_index].x - (currentPaddleWidth>>1) + rndPaddleOffset,maxSpeed);
}
void GenNextLevel_prog1(char procLevel)
{
    BrickLayout bl;
#if DEMOMODE
    if (gameState!=STATE_ATTRACT){
        if (!demoModeUnlock){ //not dks paddle connected
            procLevel = (procLevel<=2) ? procLevel : 2;//for demo 0,1,2
            if (procLevel == 2){
                demoModeFrames = 0;
            }
        }
    }
#endif
    bl = brickLayouts[procLevel % NUM_BRICK_LAYOUTS];
    currentBrickWidth = bl.brickWidth;
    currentNumBricksH = bl.numColumns;
    BreakoutBricksSetupWrapper_prog1(BRICKSSPRITEBANK);
    increase_ball_speed_prog1(procLevel);
    // Every 2 waves: paddle shrinks
    if ((procLevel > 0) && ((procLevel) % 2 == 0)) {//even numbered levels > 0 shrink paddle
        shrink_paddle_prog1();
    }
    generate_paddle_sprite_prog1();
}

#define ENABLEPADDLEHIGHESTBALL false
#define ENABLEPADDLECLOSESTBALL false
#define ENABLEPADDLEDOWNESTBALL false

void increase_ball_speed_prog1(unsigned char level)
{
    unsigned char i=0;
    reset_reflect_angles_prog1();//start with clean slate
    for (i=0; i<NUM_ANGLES; i++){
        angle temp;
        temp.dx = (currentReflectAngles_prog1[i].dx * (50 + level)) / 50;//2% increase
        temp.dy = (currentReflectAngles_prog1[i].dy * (50 + level)) / 50;
        currentReflectAngles_prog1[i] = temp;
    }
}
    
void NewLevel_prog1()
{
    unsigned char x;
    unsigned char y;
    int count = 0;
    for (y = 0; y<NUM_BRICK_ROWS; y++){
        for (x=0; x<currentNumBricksH; x++){
            brickRows[y].visible[x] = 1;
            brickRows[y].erase_frames[x] = 0;
        }
    }
    erase_queue_count=0;
    anim_queue_count=0;
    bricks_remaining = currentNumBricksH * NUM_BRICK_ROWS;

    init_ball_system_prog1();
    breakoutRefresh=0;
    breakoutBricksRefresh=0;
    scoreRefreshFrames=2;
    hiscoreRefreshFrames=2;
    livesRefreshFrames=2;
    magicBrickIndex = rnd_range(0,(currentNumBricksH * NUM_BRICK_ROWS-1));

    //lives_left++;//hacky bullshit
}
#pragma code-name (pop)


#pragma code-name (push, "PROG2")
void inputButtonsDraw_prog2()
{
    //input testing
    if (player1_buttons & INPUT_MASK_PADDLE_UP){queue_draw_box(1,7,8,2,182);}
    if (player1_buttons & INPUT_MASK_PADDLE_DOWN){queue_draw_box(11,7,8,2,182);}
    if (player1_buttons & INPUT_MASK_PADDLE_LEFT){queue_draw_box(21,7,8,2,182);}
    if (player1_buttons & INPUT_MASK_PADDLE_RIGHT){queue_draw_box(31,7,8,2,182);}
    if (player1_buttons & INPUT_MASK_PADDLE_A){queue_draw_box(41,7,8,2,182);}
    if (player1_buttons & INPUT_MASK_PADDLE_B){queue_draw_box(51,7,8,2,182);}
    if (player1_buttons & INPUT_MASK_PADDLE_C){queue_draw_box(61,7,8,2,182);}
    if (player1_buttons & INPUT_MASK_PADDLE_START){queue_draw_box(71,7,8,2,182);}
    if (player1_buttons & INPUT_MASK_PADDLE_X){queue_draw_box(81,7,8,2,199);}//x
    if (player1_buttons & INPUT_MASK_PADDLE_Y){queue_draw_box(91,7,8,2,199);}//y
    if (player1_buttons & INPUT_MASK_PADDLE_Z){queue_draw_box(101,7,8,2,199);}//z
    if (player1_buttons & INPUT_MASK_PADDLE_MODE){queue_draw_box(111,7,8,2,199);}//mode

}
void inputBinaryDraw_prog2()
{
    //inputs mapped as 7 bytes from low to high (right to left)
    //up,down,left,right,a,b,c
    //128 possibilities, should map cleanly to pixels from left to right
    queue_draw_box(0,BINARYTESTPOSY,button_byte>>1,1,182);
}

#pragma code-name (pop)



#pragma code-name (push, "PROG1")
// void ToggleDemoMode_prog1()
// {
//     if (player1_buttons & INPUT_MASK_START && ~player1_old_buttons & INPUT_MASK_START)
//     {
//         demoMode = !demoMode;
//     }
// }

// void NewGame_prog1()
// {
//     lives_left = 3;
//     score = 0;
// }
void init_game_prog1()//used to refresh the screen when all the bricks are gone
{
    cycleLevel=0;
    lives_left = 3;
    newLevelTimer = 100;
    score = 0;
    currentPaddleWidth = STARTPADDLEWIDTH;
    hiscore = (hiscore<100) ? 100 : hiscore;//default 100
    subState = SUBSTATE_NEW_LEVEL;
    reset_reflect_angles_prog1();
}
void DrawBricks_prog1(){//only gets used to refresh the whole fully-populated field
    unsigned char row;
    //queue_draw_sprite(1,BRICK_TOP,126,BRICKHEIGHT*NUMBRICKSV,1,BRICK_TOP,2);//bg in bank 2
    for (row=0;row<NUM_BRICK_ROWS;row++)  
    {
        unsigned char posy = BRICK_TOP + BRICK_HEIGHT * row;//brickRow[y].posy;//brickRowYPos[y];
        unsigned char col;
        unsigned char animFrame;
        for (col=0;col<currentNumBricksH;col++)
        {
            // if (!brickRows[row].visible[col]){
            //     animFrame = animatedBricks[row*currentNumBricksH + col];
            //     if (animFrame)
            //     {
            //         unsigned char xDrawStart = BRICK_LEFT + col * currentBrickWidth;
            //         unsigned char gx = ((20-animFrame)>>1)*currentBrickWidth;//calculate the growing x offset based on the animation frame (0-6)
            //         queue_draw_sprite(BRICK_LEFT + col * currentBrickWidth,posy,currentBrickWidth,BRICK_HEIGHT,gx,(row*BRICK_HEIGHT)+80,3);
            //         animatedBricks[row*currentNumBricksH + col]--;
            //     }
            // }
            // else
            {//if current brick is visible, draw the box
                unsigned char numBricksWidth = 1;
                unsigned char newX = col;
                unsigned char xDrawStart=BRICK_LEFT;
                unsigned char brickDrawWidth;
                if (col<currentNumBricksH-1){//if it isn't the last box check the next brick in the sequence
                    bool done = false;
                    while (!done){
                        if (newX>=currentNumBricksH-1){//end
                            done = true;
                        } else if (!brickRows[row].visible[newX+1]){
                            done = true;
                        }
                        else {
                            numBricksWidth++;
                            newX++;
                        }              
                    }
                }//if it is NUMBRICKSH-1 brick it will draw it
                //if (numBricksWidth>NUMBRICKSH) numBricksWidth=NUMBRICKSH;//total hack
                //queue_draw_box(brickColumnPos[col], posy, BRICKWIDTH, BRICKHEIGHT, rowColor);

                xDrawStart = BRICK_LEFT + col * currentBrickWidth;
                brickDrawWidth = currentBrickWidth * numBricksWidth;
                if (xDrawStart + brickDrawWidth > 127) brickDrawWidth-=1;//last brick index is one pixel short?

                //queue_draw_box(xDrawStart,posy,brickDrawWidth,BRICKHEIGHT,rowColor);
                queue_draw_sprite(xDrawStart,posy,brickDrawWidth,BRICK_HEIGHT,xDrawStart,(row*BRICK_HEIGHT)/*+60*/,BRICKSSPRITEBANK);

                col=newX;
            }
        }
    }
}
#pragma code-name (pop)

#pragma code-name (push, "PROG1")
void BrickHeal_prog1(unsigned char posx, unsigned char posy, unsigned char offset){
    unsigned char row;
    unsigned char col;
    unsigned char prevRow;
    unsigned char prevCol;
    unsigned char posXArray[2];
    unsigned char posYArray[2];
    char i,j=0;
    // unsigned char newX = BRICK_LEFT+(BRICKWIDTH * col[0]);//
    // unsigned char newY = BRICK_TOP+(BRICKHEIGHT * row[0]);
    
    //should check 4 corners of ball
    posXArray[0]=posx;
    posXArray[1]=posx + BALLSIZE-1;
    posYArray[0]=posy;
    posYArray[1]=posy + BALLSIZE-1;

    for (i=0;i<2;i++){
        for (j=0;j<2;j++){
            if (posYArray[j] >= BRICK_TOP && posYArray[j] <= BRICK_BOTTOM){
                row = (posYArray[j] - BRICK_TOP)/BRICK_HEIGHT;
                col = (posXArray[i] - BRICK_LEFT)/currentBrickWidth;
                if (!(row == prevRow && col == prevCol)){//avoid redundant consecutive draws
                    prevRow = row;
                    prevCol = col;
                    if (brickRows[row].visible[col])
                    {
                        queue_draw_sprite(BRICK_LEFT + col * currentBrickWidth,BRICK_TOP + BRICK_HEIGHT * row,currentBrickWidth,BRICK_HEIGHT,BRICK_LEFT + col * currentBrickWidth,row*BRICK_HEIGHT+offset,BRICKSSPRITEBANK);//paint box
                    }
                }
            } 
        }
    }
}
#pragma code-name (pop)

#pragma code-name (push, "PROG1")
void DrawBricksErase_prog1(){ //erase bricks in the queue
    //unsigned char row;
    //unsigned char col;
    //int idx;
    unsigned char posy;
    int i;

    for (i = 0; i < erase_queue_count; i++) 
    {
        posy = erase_queue[i].y;//BRICK_TOP + BRICK_HEIGHT * row;//brickRow[y].posy;//brickRowYPos[y];

        // Draw the "hole" in the current back-buffer
        EraseRect(erase_queue[i].x,posy,erase_queue[i].w,erase_queue[i].h);
        erase_queue[i].frames_left--;

        // If it's been erased from both buffers, remove it from the queue
        if (erase_queue[i].frames_left == 0) {
            erase_queue[i] = erase_queue[erase_queue_count - 1]; // Swap with last
            erase_queue_count--;
            i--; // Re-check this slot since we swapped
        }
    }

}
#pragma code-name (pop)


#pragma code-name (push, "PROG1")
void DrawBricksWrapper_prog1(){
    if (breakoutBricksRefresh<4){//seems to clean things up to let it go a few extra frames
        DrawBricks_prog1();
        breakoutBricksRefresh++;
    }
    else
    {
        #if BRICK_ANIMATION
            process_anim_queue_v2_prog1();
        #endif
        DrawBricksErase_prog1();//erase bricks that didn't make it into the anim queue

    }
}
#pragma code-name (pop)

#pragma code-name (push, "PROG1")
void incrementScore_prog1(char num){
    scoreRefreshFrames=2;
    score += num;
    if (gameState==STATE_BREAKOUT_PLAY){
        if (score > hiscore){
            hiscore = score;
            hiscoreRefreshFrames=2;
        }
    }
}
void addLives(char num)
{
    livesRefreshFrames=2;//trigger a refresh
    lives_left = lives_left + num;//num could be negative
    lives_left = (lives_left<0) ? 0 : lives_left;//safety check
}
void check_brick_collision_prog1(char *_ball_x, char *_ball_y, int *_ball_dx, int *_ball_dy, int *_ballSpeed) {
    char col;
    char row;
    unsigned char ballCenterX;
    unsigned char ballCenterY;
    unsigned char ballXMinMax[2];// = {*_ball_x,*_ball_x + BALLSIZE-1};
    unsigned char ballYMinMax[2];// = {*_ball_y,*_ball_y + BALLSIZE-1};
    unsigned char i=0;
    unsigned char j=0;
    bool collisionFound = false;
    ballXMinMax[0] = *_ball_x;
    ballXMinMax[1] = *_ball_x + BALLSIZE-1;
    ballYMinMax[0] = *_ball_y;
    ballYMinMax[1] = *_ball_y + BALLSIZE-1;
    
    if (ballYMinMax[1] < BRICK_TOP || ballYMinMax[0] > BRICK_BOTTOM) return; //false
    if (ballXMinMax[1] < BRICK_LEFT || ballXMinMax[0] > BRICK_RIGHT) return; // false //redundant

    //check collision on 4 bounding corners but only recognize the first collision

    //if (BALLSIZE==2) {//picks collision point based on direction for 2x2 ball
        // ballCenterX = (*_ball_dx>0) ? *_ball_x+1 : *_ball_x;
        // ballCenterY = (*_ball_dy>0) ? *_ball_y+1 : *_ball_y;
    //} else { //assume ball will never be 1 pixel
    //    ballCenterX = *_ball_x + BALLCENTEROFFSET;
    //    ballCenterY = *_ball_y + BALLCENTEROFFSET;
    //}

        // Quick reject if ball not in brick zone
    // if (ballCenterY < BRICK_TOP || (ballCenterY) > BRICK_BOTTOM) return false;
    // if (ballCenterX < BRICK_LEFT || ballCenterX > BRICK_RIGHT) return false;
    

    //check 4 collisions and exit when first found else return
    // for (j=0;j<2;j++){
    //     for (i=0;i<2;i++){
            ballCenterX = ballXMinMax[i];
            ballCenterY = ballYMinMax[j];
            // Convert to brick coordinates
            col = (ballCenterX - BRICK_LEFT) / currentBrickWidth;//>> 3;        // 0-15
            row = (ballCenterY - BRICK_TOP) / BRICK_HEIGHT;
            if (row>=0 && col>=0){//both must be positive
                if (brickRows[row].visible[col]) 
                {
                    collisionFound = true;
                    //break;
                }
            }
    //     }
    // }
    if (!collisionFound) return; //false

    // Convert to brick coordinates
    // col = (ballCenterX - BRICK_LEFT) / BRICKWIDTH;//>> 3;        // 0-15
    // row = (ballCenterY - BRICK_TOP) / BRICKHEIGHT;  
    
    if (brickRows[row].visible[col])//redundant
    {
        unsigned char brick_top;
        unsigned char brick_bottom;
        unsigned char brick_left;
        unsigned char brick_right;
        unsigned char dist_left;
        unsigned char dist_right;
        unsigned char dist_top;
        unsigned char dist_bottom;
        unsigned char min_dist;
        unsigned char side;  // 0=left,1=right,2=top,3=bottom
        //uint8_t hitIndex = get_brick_index_asm(ballCenterX, ballCenterY);//magic brick check
        //uint8_t hitIndex = get_brick_index_prog1(ballCenterX, ballCenterY);//magic brick check
        uint8_t hitIndex = GET_BRICK_INDEX_PROG1(ballCenterX, ballCenterY);
        // Destroy brick
        brickRows[row].visible[col] = 0;
        brickRows[row].erase_frames[col] = 2;
        //animatedBricks[row*currentNumBricksH + col] = 20;//start animation for this brick //animate at half speed
        
        if (BRICK_ANIMATION && (anim_queue_count < ANIM_QUEUE_MAX))
        {
            anim_queue[anim_queue_count].frames_left = NUM_ANIM_FRAMES*ANIM_FRAME_MULTIPLIER+2;//double buffers?
            anim_queue[anim_queue_count].x = BRICK_LEFT + (col * currentBrickWidth);
            anim_queue[anim_queue_count].y = BRICK_TOP + (row * BRICK_HEIGHT);
            anim_queue[anim_queue_count].w = currentBrickWidth;
            anim_queue[anim_queue_count].h = BRICK_HEIGHT;
            anim_queue_count++;
        } else if (erase_queue_count < ERASE_QUEUE_MAX) {
            erase_queue[erase_queue_count].frames_left = 2; // Two buffers to clean
            erase_queue[erase_queue_count].x = BRICK_LEFT + (col * currentBrickWidth);
            erase_queue[erase_queue_count].y = BRICK_TOP + (row * BRICK_HEIGHT);
            erase_queue[erase_queue_count].w = currentBrickWidth;
            erase_queue[erase_queue_count].h = BRICK_HEIGHT;
            erase_queue_count++;
        }

        bricks_remaining--;//counting bricks

        // Calculate brick boundaries
        brick_top = BRICK_TOP + (row << 2);//assume row height of 4
        brick_bottom = brick_top + BRICK_HEIGHT;//was + 3  assume row height of 4
        brick_left = BRICK_LEFT + (col * currentBrickWidth);
        brick_right = brick_left + (currentBrickWidth);
        
        // Calculate distances to each edge
        dist_left = *_ball_x - brick_left;
        dist_right = brick_right - *_ball_x;
        dist_top = *_ball_y - brick_top;
        dist_bottom = brick_bottom - *_ball_y;

        // Find closest edge
        min_dist = dist_left;
        side = 0;  // 0=left,1=right,2=top,3=bottom
        if (dist_right < min_dist) { min_dist = dist_right; side = 1; }
        if (dist_top < min_dist) { min_dist = dist_top; side = 2; }
        if (dist_bottom < min_dist) { side = 3; }

        switch(side) 
        {
            case 0: //*_ball_x = brick_left - 1; 
            case 1:
                *_ball_dx = -(*_ball_dx);   
                //break;  // Left
            //case 1: //*_ball_x = brick_right + 1; 
                *_ball_dx = -(*_ball_dx);
                break;  // Right
            case 2: //*_ball_y = brick_top - 1; 
            case 3:
                break;    // Top
            //case 3: //*_ball_y = brick_bottom + 1; 
              //  break; // Bottom
        }
        if (hitIndex == magicBrickIndex) {
            angle randomAngle;
            randomAngle = get_random_spawn_angles_prog1();
            spawn_new_ball_prog1(*_ball_x,*_ball_y,randomAngle.dx,randomAngle.dy, *_ballSpeed);
        }
        *_ball_dy = -(*_ball_dy);
        //stop_music();
        playBass(row);
        //soundTestA();
        //play_single_note();
        //score+= brickRowPoints[row];//brickRows[row].points;//(7-row)>>1;
        incrementScore_prog1(brickRowPoints[row]);
        if (row==0) *_ballSpeed = 3;//not double speed but 3/2 speed
    }
    return;
}
#pragma code-name (pop)
void playNoteSequence(unsigned char style){
    unsigned char x;
    signed char i;
    unsigned char noteLength=2;
    //style 0 descending
    //style 1 ascending
    //style 2 random
    if (style==0){
        for (i=0;i<=5;i++)
        {
            if (i>=0)playBass(i);
            for(x=0;x<noteLength;x++){
                tick_music();
                await_vsync(1);
            }
        }
    }
    else if (style==1)
    {
        for (i=5;i>=0;i--)
        {
            if (i>=0)playBass(i);
            for(x=0;x<noteLength;x++){
                tick_music();
                await_vsync(1);
            }
        }
    }
    else if (style==2)//random
    {
        for (i=0;i<=5;i++)
        {
            if (i>=0)playBass(rnd_range(0,5));
            for(x=0;x<noteLength;x++){
                tick_music();
                await_vsync(1);
            }
        }
    }
}
#pragma code-name (push, "PROG1")
void BreakoutBGRefresh_prog1()
{
    if (breakoutRefresh<2){
        EraseRect(1,7,127,120);//replace the screen with pre-rendered sprite
        breakoutRefresh++;//runs twice to cover two buffers
    }
    else//clean up edges
    {
        EraseRect(2,(CEILING-2),124,2);
        EraseRect(0,7,BRICK_LEFT,127);
        EraseRect(BRICK_RIGHT+1,7,3,113);

    }
}
void update_player_paddle_prog1(){
    if ( gameState==STATE_ATTRACT){
        paddleX = paddleXFromLowestBall_prog1();
    } else {
        if (controller_style == CONTROLLER_STYLE_PADDLE0){
            paddleX = paddleXFromPot8opt_prog1(button_byte);
        }
        else if (controller_style == CONTROLLER_STYLE_GPAD1)
        {
            paddleX = paddleXFromDPad_prog1(cycleLevel);
        }
    }
}
void spawn_new_ball_prog1(int x, int y, int dx, int dy, unsigned char speed) {
    unsigned char i;
    for (i = 0; i < MAX_BALLS; i++) {
        if (!(balls[i].active || balls[i].was_active_old || balls[i].was_active_old_old)) {
            balls[i].x = x;
            balls[i].y = y;
            balls[i].x_old = x;
            balls[i].x_old_old = x;
            balls[i].y_old = y;
            balls[i].y_old_old = y;
            balls[i].dx = dx;
            balls[i].dy = dy;
            balls[i].speed = speed;
            balls[i].active = true;
            balls[i].was_active_old = false;
            balls[i].was_active_old_old = false;
            soundWall();
            return; 
        }
    }
}
void spawn_new_rnd_ball_prog1() {
    unsigned char _x; 
    unsigned char _y; 
    int _dx; 
    int _dy; 
    unsigned char _speed;
    unsigned char i;
    _x = rnd_range(5,114);
    _y = rnd_range(64,68);
    _dx = (rnd_range(0,10) > 5) ? 255 : -255;
    _dy = 255;//always down
    _speed = 1;//default slow
    
    for (i = 0; i < MAX_BALLS; i++) {
        if (!(balls[i].active)){//} || balls[i].was_active_old || balls[i].was_active_old_old)) {
            balls[i].x = _x;
            balls[i].y = _y;
            balls[i].x_old = _x;
            balls[i].x_old_old = _x;
            balls[i].y_old = _y;
            balls[i].y_old_old = _y;
            balls[i].dx = _dx;
            balls[i].dy = _dy;
            balls[i].speed = _speed;
            balls[i].active = true;
            balls[i].was_active_old = false;
            balls[i].was_active_old_old = false;
            soundWall();
            return; 
        }
    }
}
void printPressStart_prog1(char x, char y, bool visible){
    if (visible){
        queue_draw_sprite(x,y,64,5,0,21,PRESSSTARTSPRITEBANK);//press start
    } else {
        EraseRect(x,y,64,5);
    }
}

void printGameOver_prog1(char x, char y, bool visible){
    if (visible){
        queue_draw_sprite(x,y,64,5,0,40,PRESSSTARTSPRITEBANK);//press start
    }else {
        EraseRect(x,y,64,5);
    }
}
void printGetReady_prog1(char x, char y, bool visible){
    if (visible){
        queue_draw_sprite(x,y,64,5,0,47,PRESSSTARTSPRITEBANK);//press start
    }else {
        EraseRect(x,y,64,5);
    }
}
void printLoading_prog1(char x, char y, bool visible){
    if (visible){
        queue_draw_sprite(x,y,64,5,0,54,PRESSSTARTSPRITEBANK);//press start
    }else {
        EraseRect(x,y,64,5);
    }
}
void printBlinkMessage_prog1(char index){
    //0 press start
    //1 game over
    //2 get ready //no blink
    //3 loading //no blink
    if (blinkTimer++>0)
    {
        if (index==0) printPressStart_prog1(32,64,true);
        else if (index==1) printGameOver_prog1(32,64,true);
        else if (index==2) printGetReady_prog1(32,64,true);
        else if (index==3) printLoading_prog1(32,64,true);
        else printGameOver_prog1(32,64,true);
        blinkMessageVisible=2;
        blinkTimer = (blinkTimer>45) ? blinkTimer=-80 : blinkTimer;
    }
    else
    {
        if (blinkMessageVisible) {
            messageEraseCount=2;
            blinkMessageVisible = 0;
        }
    }
}

#pragma code-name (pop)
void eraseBlinkMessage(char *counter)
{
    if (*counter>0){
        printPressStart_prog1(32,64,false);//prints a blank box
        *counter = *counter - 1;
    }
}

#pragma code-name (push, "PROG2")
void SelectControllerScreen_prog2(bool buttonAPress, bool buttonBPress)
{
    static unsigned char timer = 0;
    unsigned char loopTime = ((timer++ >> 2) & 6);
    unsigned char flipLoopTime = 7-loopTime;

    queue_draw_sprite(0,0,127,127,0,0,TITLESPRITEBANK);
    //marching ants
    queue_draw_sprite(6,86,115,1,0,87 + loopTime,PRESSSTARTSPRITEBANK);
    queue_draw_sprite(6,108,115,1,0,87 + flipLoopTime,PRESSSTARTSPRITEBANK);
    queue_draw_sprite(6,86,1,22,88+flipLoopTime,0,PRESSSTARTSPRITEBANK);
    queue_draw_sprite(121,86,1,22,88+loopTime,0,PRESSSTARTSPRITEBANK);
    if (isEmulator){
    if  (CHK_EMU & 0x02) //a joystick is connected
    {
        //draw A at 10,97
        queue_draw_sprite(10,95,5,5,0,78,PRESSSTARTSPRITEBANK);
        //draw B at 64,97
        queue_draw_sprite(64,95,5,5,6,78,PRESSSTARTSPRITEBANK);
    }
    else //fall back to keyboard Z,X
    {
        //draw z at 10,97
        queue_draw_sprite(10,95,5,5,24,78,PRESSSTARTSPRITEBANK);
        //draw x at 64,97
        queue_draw_sprite(64,95,5,5,30,78,PRESSSTARTSPRITEBANK);
    }
    }
    if (buttonBPress) 
    {
        Set_emulator_paddle_mode(false);
        controller_style = CONTROLLER_STYLE_GPAD1;
        gameState = STATE_ATTRACT;
        breakoutRefresh=0;//triggers a bg refresh
        breakoutBricksRefresh=0;//triggers bricks refresh
        if (isEmulator){trigger_rumble(255);}
        playNoteSequence(2);

    }
    else if (buttonAPress) 
    {
        Set_emulator_paddle_mode(true);
        controller_style = CONTROLLER_STYLE_PADDLE0;
        gameState = STATE_ATTRACT;
        breakoutRefresh=0;//triggers a bg refresh
        breakoutBricksRefresh=0;//triggers bricks refresh
        playNoteSequence(2);
    }
}
#pragma code-name (pop);


#pragma rodata-name (push, "PROG1")
unsigned char dissolve_mask[MAX_NOISE];//sample largest brick size 15x4? 8 columns
#pragma rodata-name (pop)

#pragma code-name (push, "PROG1")

void bake_gradient_mask_prog1() {//15 is theoretical largest possible brick width
    char i;
    // Fill a 2x4 (or 8x4) area with randomized threshold values
    for(i = 0; i < MAX_NOISE; i++) {
        dissolve_mask[i] = rnd_range(0,255);
    }
}

void bake_animation_frames_v2_prog1() 
{
    char oldFlags = flagsMirror; // Save the original screen/DMA settings
    char oldBanks = banksMirror; 
    unsigned int animOffset = 24*128;//start of animation frames in bank 3
    unsigned char x,y;
    //unsigned char pixel;

    // 1. SET THE HARDWARE MODE
    // Clear Bit 0 (DMA_ENABLE) and Clear Bit 1 (DMA_CPU_TO_VRAM)
    flagsMirror &= ~(DMA_ENABLE | DMA_CPU_TO_VRAM); 
    *dma_flags = flagsMirror;

    // 2. BANK IN THE SPRITES
    banksMirror = bankflip | GRAM_PAGE(BRICKSSPRITEBANK);
    *bank_reg = banksMirror;

    //v2
    //don't need to copy pixels, but instead place black (or bg) pixels over transparency
    //they no longer need to be arranged horizontally per row because we will only be eating away at the pixels in vram

    //change the animation orientation threshold, frames get drawn vertically 4-pixel rows
    for (y=0; y<104; y++)//fill up erosion vault
    {
        unsigned char numFrames = NUM_ANIM_FRAMES;//26;//(NUM_ANIM_FRAMES<=104/4) ? NUM_ANIM_FRAMES : 104/4;//limit to the 104 available rows
        unsigned char frame=y>>2;
        unsigned int threshold = ((frame+2) * (256/numFrames));//(frame * 255/numFrames);//+2 makes sure it starts eaten and ends blank
        unsigned int thresholdDim = ((frame+4) * (256/numFrames));//(frame * 255/numFrames);//+2 makes sure it starts eaten and ends blank
        unsigned char maxWidth = 128;//128 if bold
        for (x = 0; x < maxWidth; x++)//write out max width needed
        {//each frame of anim is 30w and 4h
            //unsigned int origCoord = (y * maxWidth) + x; //only needs to be the first frame over and over
            unsigned int targetCoord = animOffset + x + (y*128); //offset begins at 24 row
            unsigned char sampleX = x & 15;//mod 16 //treating 64 values as 16x4 block
            unsigned char sampleY = y & 3;
            unsigned char noise = dissolve_mask[sampleX + (sampleY<<4)];//dissolve_mask[(x+(y*maxWidth))&(MAX_NOISE-1)] ;//no offset
            {
                if (noise <= threshold) {
                    vram[targetCoord] = 0b00100000;//0b11111111;//0b00100000; // Erase the pixel
                }
                else if (noise <= thresholdDim) {
                    vram[targetCoord] = vram[targetCoord] & 0b11110011;//0b11111111;//0b00100000; // Erase the pixel
                }  
                else {
                    vram[targetCoord] = 0;//pixel; //transparent pixel
                }
            }
        }
    }

    // 4. RESTORE EVERYTHING
    flagsMirror = oldFlags;
    banksMirror = oldBanks;
    *dma_flags = flagsMirror;
    *bank_reg = banksMirror;

}




#pragma code-name (pop);

#pragma code-name (push, "PROG1")

void process_anim_queue_v2_prog1() {
    unsigned char i = anim_queue_count;
    unsigned char animOffsetY = 24;
    unsigned char frameOffset=0;
    unsigned char animFrame;
    unsigned char row;
    unsigned char stagger;
    unsigned char tx, ty, tw, th, tfl;
    unsigned char rel_x, rel_y;
    EraseTask *task;
    
    //for (i = 0; i < anim_queue_count; i++) {
    while (i--) {// Iterating backwards is slightly faster on 6502
        task = &anim_queue[i];
        // Cache values to local registers/Zero Page
        tx = task->x;
        ty = task->y;
        tw = task->w;
        th = task->h;
        tfl = task->frames_left;
        
        rel_x = tx - BRICK_LEFT;
        rel_y = ty - BRICK_TOP;

        row = rel_y >> 2;
        stagger = stagger = rel_y & 4;//(row << 2) & 7;
        // 3. Draw the resulting sprite to the screen
        //redraw the brick //not needed, this was a mistake
        //queue_draw_sprite(tx,ty,tw,th,rel_x, rel_y, BRICKSSPRITEBANK);
        
        animFrame = NUM_ANIM_FRAMES - (tfl/ANIM_FRAME_MULTIPLIER-2);
        frameOffset = (animFrame * 4);//vertical offset by 4 pixels per frame
        
        //eat away at the brick
        queue_draw_sprite(tx,ty,tw,th, (rel_x + stagger) & (127 - tw), animOffsetY + frameOffset,BRICKSSPRITEBANK);//brick sprite has no y offset
        task->frames_left = --tfl;

        // 5. Cleanup: If 0, remove from queue (standard swap-and-pop)
        if (tfl == 0) {
            anim_queue[i] = anim_queue[--anim_queue_count];
            //i--; // Re-check this index since we swapped
        }
    }
}
#pragma code-name (pop);

#pragma code-name (push, "PROG1")
void bevel_all_bricks_prog1() {
    char oldFlags = flagsMirror; // Save the original screen/DMA settings
    char oldBanks = banksMirror; 
    int row, col, y,x;

    // 1. SET THE HARDWARE MODE
    // Clear Bit 0 (DMA_ENABLE) and Clear Bit 1 (DMA_CPU_TO_VRAM)
    flagsMirror &= ~(DMA_ENABLE | DMA_CPU_TO_VRAM); 
    *dma_flags = flagsMirror;

    // 2. BANK IN THE SPRITES
    banksMirror = bankflip | GRAM_PAGE(BRICKSSPRITEBANK);
    *bank_reg = banksMirror;

    // 3. THE PAINT LOOP (128x24 block)
    for (row = 0; row < 6; row++) {
        int row_start = 0x000 + (row * 4 * 128);//was 0x181
        int pixel = vram[row_start];//color is constant in the entire row
        for (y = 0; y < 4; y++) {//loop 4 lines of each row
            for (col = 4; col < 124; col += currentBrickWidth) {//leap from brick to brick horizontally
                int pos = row_start + (y * 128) + col;
                if (pixel != 0) {
                    //vram[pos] = ((pixel & 0b00000111)<7) ? pixel + 1 : pixel & 0b11101111;     // Highlight
                    //vram[pos + currentBrickWidth-1] = ((pixel & 0b00000111)>0) ? pixel - 1 : pixel; // Shadow
                    vram[pos + currentBrickWidth-1] = ((pixel & 0b00000111)<7) ? pixel + 1 : pixel & 0b11101111;     // Highlight
                    vram[pos] = ((pixel & 0b00000111)>0) ? pixel - 1 : pixel; // Shadow
                }
            }
            //top and bottom shading overrides vertical
            if (y==0){
                for (x=0; x<128;x++){
                    vram[row_start + (y * 128) + x] = ((pixel & 0b00000111)<7) ? pixel + 1 : pixel & 0b11101111; //hilite
                }
            } else if (y==3){
                for (x=0; x<128;x++){
                    vram[row_start + (y * 128) + x] = ((pixel & 0b00000111)>0) ? pixel - 1 : pixel; // Shadow
                }
            }

        }
    }

    // 4. RESTORE EVERYTHING
    flagsMirror = oldFlags;
    banksMirror = oldBanks;
    *dma_flags = flagsMirror;
    *bank_reg = banksMirror;
}

void BreakoutBricksSetupWrapper_prog1(char bank)
{
    loading_screen_prog1();
    load_spritesheet(ASSET__gfx__BreakoutBrickWide_bmp,bank);
    bevel_all_bricks_prog1();
}
#pragma code-name (pop);


#pragma code-name (push, "PROG1")
void process_all_balls_prog1() {
    unsigned char i; 
    isBallWentOutOfBounds = false;
    active_balls_count=0;
    for (i = 0; i < MAX_BALLS; i++) {
        if (balls[i].active) {
            update_ball_logic_prog1(&balls[i]);
            if (balls[i].active) active_balls_count++;
        }
    }
    if (!active_balls_count && isBallWentOutOfBounds){ 
        addLives(-1);
    }
    
}
void render_balls_prog1() {
    unsigned char i;
    for (i = 0; i < MAX_BALLS; i++) {
        if (balls[i].active) {
            // Only draw the ones currently in play
            //draw_sprite(balls[i].x, balls[i].y, BALL_SPRITE_ID);
            queue_draw_sprite(balls[i].x, balls[i].y,BALLSIZE,BALLSIZE,4,0,PADDLESPRITEBANK);//hacky use of paddle sprite bank
        }
    }
}
void generate_paddle_sprite_prog1(){
//original paddle sprite box
//0,0 to 15,3
//paint new paddle at 0,4
    char oldFlags = flagsMirror; // Save the original screen/DMA settings
    char oldBanks = banksMirror; 
    unsigned char x,y;
    unsigned char huesat = PADDLECOLOR & 0b11111000;//0b10101000;//0 yellow 1 orange 2 pink 3 purple 4 blue 5 blue 7 grn

    // 1. SET THE HARDWARE MODE
    // Clear Bit 0 (DMA_ENABLE) and Clear Bit 1 (DMA_CPU_TO_VRAM)
    flagsMirror &= ~(DMA_ENABLE | DMA_CPU_TO_VRAM); 
    *dma_flags = flagsMirror;

    // 2. BANK IN THE SPRITES
    banksMirror = bankflip | GRAM_PAGE(PADDLESPRITEBANK);
    *bank_reg = banksMirror;

    for (y=0; y<4; y++){//copy 4 rows
        //copy left two columns from 0,0
        unsigned int sourceCoord = 0 + (y*128);
        unsigned int targetCoord = 0 + ((y+4)*128);//target paddle starts at row 4
        
        vram[targetCoord] = (vram[sourceCoord]==0) ? 0 : (vram[sourceCoord] & 0b00000111) + huesat;
        sourceCoord = 1 + (y*128);
        targetCoord = 1 + ((y+4)*128);//target paddle starts at row 4
        vram[targetCoord] = (vram[sourceCoord]==0) ? 0 : (vram[sourceCoord] & 0b00000111) + huesat;

        //copy last two columns from 14,0 
        {
        unsigned int sourceXOffset = 14;
        unsigned int targetXOffset = currentPaddleWidth - 2;
        sourceCoord = sourceXOffset + (y*128);
        targetCoord = targetXOffset + ((y+4)*128);//target paddle starts at row 4
        vram[targetCoord] = (vram[sourceCoord]==0) ? 0 : (vram[sourceCoord] & 0b00000111) + huesat;
        sourceCoord = sourceXOffset + 1 + (y*128);
        targetCoord = targetXOffset + 1 + ((y+4)*128);//target paddle starts at row 4
        vram[targetCoord] = (vram[sourceCoord]==0) ? 0 : (vram[sourceCoord] & 0b00000111) + huesat;
        }
        for (x = 2; x<currentPaddleWidth-2;x++){//copy the middle
            unsigned int targetCoord = x + ((y+4)*128);//target paddle starts at row 4
            //copy the 3rd column
            sourceCoord = 2 + (y*128);
            vram[targetCoord] = (vram[sourceCoord] & 0b00000111) + huesat;

        }

        // for (x = 2; x<currentPaddleWidth-2;x++){//white in the middle
        //     unsigned int targetCoord = x + ((y+4)*128);//target paddle starts at row 4
            
        //     vram[targetCoord] = 0b00000111 + huesat;
        // }
        for (x = currentPaddleWidth; x<128;x++){//clean the rest of the line
            unsigned int targetCoord = x + ((y+4)*128);//target paddle starts at row 4
            vram[targetCoord] = 0b00000000;
        }

    }
    // 4. RESTORE EVERYTHING
    flagsMirror = oldFlags;
    banksMirror = oldBanks;
    *dma_flags = flagsMirror;
    *bank_reg = banksMirror;
}

void render_paddle_prog1() 
{
    queue_draw_sprite(paddleX,PADDLEY,currentPaddleWidth,PADDLEHEIGHT,0,4,PADDLESPRITEBANK);
}
void erase_paddle_prog1(){
    EraseRect(paddleX_old_old,PADDLEY,currentPaddleWidth,PADDLEHEIGHT);
}
void erase_balls_prog1(){
    unsigned char i;
    for (i = 0; i < MAX_BALLS; i++) {
        if (balls[i].active || balls[i].was_active_old || balls[i].was_active_old_old) {
            EraseRect(balls[i].x_old_old ,balls[i].y_old_old,BALLSIZE,BALLSIZE);
            //BrickHeal_prog1(balls[i].x_old_old ,balls[i].y_old_old,0);
        }
    }
    for (i = 0; i < MAX_BALLS; i++) {
        if (balls[i].active || balls[i].was_active_old || balls[i].was_active_old_old) {
            //EraseRect(balls[i].x_old_old ,balls[i].y_old_old,BALLSIZE,BALLSIZE);
            BrickHeal_prog1(balls[i].x_old_old ,balls[i].y_old_old,0);
        }
    }
}
void init_ball_system_prog1() {
    unsigned char i;
    for (i = 0; i < MAX_BALLS; i++) {
        balls[i].x = 0;
        balls[i].y = 0;
        balls[i].x_old = 0;
        balls[i].x_old_old = 0;
        balls[i].y_old = 0;
        balls[i].y_old_old = 0;
        balls[i].dx = 0;
        balls[i].dy = 0;
        balls[i].speed = 1;
        balls[i].active = false;
        balls[i].was_active_old = false;
        balls[i].was_active_old_old = false;
    }
}
void BakeBreakoutBackground_prog1(char bank){
    char oldFlags = flagsMirror; // Save the original screen/DMA settings
    char oldBanks = banksMirror; 
    unsigned char x,y;
    char livesArray[17*4];
    #if DEMOMODE
        char demoArray[23*5];
    #endif
    
    #if LOADSPRITEBG
    load_spritesheet(ASSET__gfx__n44thumb_bmp,bank);
    #endif

    // 1. SET THE HARDWARE MODE
    // Clear Bit 0 (DMA_ENABLE) and Clear Bit 1 (DMA_CPU_TO_VRAM)
    flagsMirror &= ~(DMA_ENABLE | DMA_CPU_TO_VRAM); 
    *dma_flags = flagsMirror;

    // 2. BANK IN THE SPRITES
    banksMirror = bankflip | GRAM_PAGE(bank);
    *bank_reg = banksMirror;

    #if !LOADSPRITEBG
    for (y=0;y<128;y++)//blank the screen
    {
        for (x=0;x<128;x++)
        {
            vram[y*128+x] = 0b00100000;
        }
    }
    #else
    for (y=0;y<=CEILING;y++)//blank the screen
    {
        for (x=0;x<128;x++)
        {
            vram[y*128+x] = 0;//0b00100000;
        }
    }
    #endif


    // queue_draw_box(1,7,128,3,0b01111000);//top border
    {
        char posx = 1;
        char posy = 7;
        char w = 128;
        char h = 3;
        char c = 0b01111000;

        for (y=posy;y<posy+h;y++)
        {
            for (x=posx;x<128;x++)
            {
                vram[y*128+x] = c;
            }
        }
    }
    // queue_draw_box(1,(CEILING-2),124,2,0b01111000);//ceiling bar - fixes ball ghosts
    {
        char posx = 1;
        char posy = (CEILING-2);
        char w = 124;
        char h = 2;
        char c = 0b01111000;

        for (y=posy;y<posy+h;y++)
        {
            for (x=posx;x<128;x++)
            {
                vram[y*128+x] = c;
            }
        }
    }
    // queue_draw_box(0,7,BRICK_LEFT,127,0b01111000);//left bar
    {
        char posx = 0;
        char posy = 7;
        char w = BRICK_LEFT;
        char h = 113;
        for (y=posy;y<posy+h;y++)
        {
            for (x=posx;x<posx+w;x++)
            {
                vram[y*128+x] = 0b01111000;
            }
        }
    }
    // queue_draw_box(BRICK_RIGHT+1,7,3,113,0b01111000);//rt bar
    {
        char posx = BRICK_RIGHT+1;
        char posy = 7;
        char w = 3;
        char h = 113;
        for (y=posy;y<posy+h;y++)
        {
            for (x=posx;x<posx+w;x++)
            {
                vram[y*128+x] = 0b01111000;
            }
        }
    }
    //load spritebank to sample "lives" text
    banksMirror = bankflip | GRAM_PAGE(PRESSSTARTSPRITEBANK);//jump to a different bank to grab "lives text"
    *bank_reg = banksMirror;
    {
        char w=17;//13;////17;
        char h=3;//5;//4;
        char xoffset = 44;//25; //44 LIFE 3px 64 LIVES 3px
        char yoffset = 96;
        for (y=0; y<h; y++){
            for (x=0; x<w; x++){
               livesArray[y*w+x]= vram[(y+yoffset)*128 + (x + xoffset)];
            }
        }
    }
    // 2. BANK IN THE SPRITES
    banksMirror = bankflip | GRAM_PAGE(bank);//back to our current bank
    *bank_reg = banksMirror;
    {
        //char vramIndex=0;
        char posx = 92;
        char posy = 12;//11
        char w=17;
        char h=3;
        for (y=0; y<h; y++){
            for (x=0; x<w; x++){
                char index=(y * w) + x;
                vram[((y + posy) * 128) + posx+x] = livesArray[index];
            }
        }
    }


    #if DEMOMODE
        if (!demoModeUnlock){ //not dks paddle connected

        //get demomode from pressStart bank, just like lives
        //queue_draw_sprite(10,120,21,5,0,27,BAKEDBACKGROUNDSPRITEBANK);
        //load spritebank to sample "lives" text
        banksMirror = bankflip | GRAM_PAGE(PRESSSTARTSPRITEBANK);//jump to a different bank to grab "lives text"
        *bank_reg = banksMirror;
        {
            char w=23;
            char h=5;
            char xoffset = 0;//25; //44 LIFE 3px 64 LIVES 3px
            char yoffset = 27;
            for (y=0; y<h; y++){
                for (x=0; x<w; x++){
                demoArray[y*w+x]= vram[(y+yoffset)*128 + (x + xoffset)];
                }
            }
        }
        banksMirror = bankflip | GRAM_PAGE(bank);//back to our current bank
        *bank_reg = banksMirror;
        {
            char posx = 5;
            char posy = 114;
            char w=23;
            char h=5;
            for (y=0; y<h; y++){
                for (x=0; x<w; x++){
                    char index=(y * w) + x;
                    vram[((y + posy) * 128) + posx+x] = demoArray[index];
                }
            }
        }
        banksMirror = bankflip | GRAM_PAGE(TITLESPRITEBANK);//side-hack into title sprite bank
        *bank_reg = banksMirror;
        {
            char posx = 5;
            char posy = 114;
            char w=23;
            char h=5;
            for (y=0; y<h; y++){
                for (x=0; x<w; x++){
                    char index=(y * w) + x;
                    vram[((y + posy) * 128) + posx+x] = demoArray[index];
                }
            }
        }
    }
    #endif

    // {//draw life bar //move to a different bank
    //     char y=17;
    //     for (x=92; x<BRICK_RIGHT; x+=2){
    //         vram[y*128 + x] = 0b10111111;
    //     }
    // }
    // 4. RESTORE EVERYTHING
    flagsMirror = oldFlags;
    banksMirror = oldBanks;
    *dma_flags = flagsMirror;
    *bank_reg = banksMirror;
}
static char digitsArray[11 * 4 * 5]; // 11 digits (0-9 plus the dot)
void bake_wave_number_prog1(unsigned char wave, int posx, int posy, char bank){
    char oldFlags = flagsMirror; // Save the original screen/DMA settings
    char oldBanks = banksMirror; 
    //char digitsArray[10*4*5];

    //prepare digits that need to be baked:
    unsigned char tens = 0;
    unsigned char ones = wave;
    unsigned char shiftLeftOffset = ((wave / 10)>0) ? 1 : 0;
    unsigned char tensDigitPosx = posx - (shiftLeftOffset * 4);
    unsigned char onesDigitPosx = posx - (shiftLeftOffset * 4);
    

    if (wave<1) return; // do nothing
    // Fast 8-bit division for numbers < 100
    while (ones >= 10) {
        ones -= 10;
        tens++;
    }
    if (tens > 0) {
        onesDigitPosx += 4;
    }

    // 1. SET THE HARDWARE MODE
    // Clear Bit 0 (DMA_ENABLE) and Clear Bit 1 (DMA_CPU_TO_VRAM)
    flagsMirror &= ~(DMA_ENABLE | DMA_CPU_TO_VRAM); 
    *dma_flags = flagsMirror;

    //load spritebank to sample "lives" text
    banksMirror = bankflip | GRAM_PAGE(PRESSSTARTSPRITEBANK);//jump to a different bank to grab "lives text"
    *bank_reg = banksMirror;
    {
        //char w=4;//4-pixel-wide digits
        //char h=50;//10 digits 4-pixels high with 1 pixel gap
        char arrayIndex=0;
        char gx = 124;//
        char x,y;
        char digit = 0;
        for (digit = 0; digit<=9; digit++){//should store the 4x5x10 column of data in an array
            char yoffset =  digit * 5;
            for (y=0; y<5; y++){
                for (x=0; x<4; x++){
                    digitsArray[arrayIndex++]= vram[(y+yoffset)*128 + (x + gx)];
                }
            }
        }
    }

    // 2. BANK IN THE SPRITES - switch to target bank
    banksMirror = bankflip | GRAM_PAGE(bank);
    *bank_reg = banksMirror;
    {//erase right half of wave line to make room for DEMO
        char h=4;
        char x,y;
        for (y=0; y<4; y++){
            for (x=64; x<BRICK_RIGHT; x++){
                //char index=(y * w) + x + gy * 4;
                vram[((y + posy) * 128) + x] = 0b00100000;
            }
        }
    }
    if (tens > 0)
    {
        char _posx = tensDigitPosx;
        char gy = tens * 5;
        char w=4;
        char h=4;
        char x,y;
        for (y=0; y<h; y++){
            for (x=0; x<w; x++){
                char index=(y * w) + x + gy * 4;
                vram[((y + posy) * 128) + _posx+x] = digitsArray[index];
            }
        }
    }
    //if (tens <= 0)
    {
        char _posx = onesDigitPosx;
        char gy = ones * 5;
        char w=4;
        char h=4;
        char x,y;
        for (y=0; y<h; y++){
            for (x=0; x<w; x++){
                char index=(y * w) + x + gy * 4;
                vram[((y + posy) * 128) + _posx+x] = digitsArray[index];
            }
        }
    }
    
    // 4. RESTORE EVERYTHING
    flagsMirror = oldFlags;
    banksMirror = oldBanks;
    *dma_flags = flagsMirror;
    *bank_reg = banksMirror;

}
void bake_num_prog1(char num, int posx, int posy, char bank){
    char oldFlags = flagsMirror; // Save the original screen/DMA settings
    char oldBanks = banksMirror; 

    unsigned char ones = num;
    unsigned char onesDigitPosx = posx ;

    // 1. SET THE HARDWARE MODE
    // Clear Bit 0 (DMA_ENABLE) and Clear Bit 1 (DMA_CPU_TO_VRAM)
    flagsMirror &= ~(DMA_ENABLE | DMA_CPU_TO_VRAM); 
    *dma_flags = flagsMirror;

    //load spritebank to sample "lives" text
    banksMirror = bankflip | GRAM_PAGE(PRESSSTARTSPRITEBANK);//jump to a different bank to grab "lives text"
    *bank_reg = banksMirror;
    {
        //char w=4;//4-pixel-wide digits
        //char h=55;//11 digits 4-pixels high with 1 pixel gap
        char arrayIndex=0;
        char gx = 124;//
        char x,y;
        char digit = 0;
        for (digit = 0; digit<=10; digit++){//should store the 4x5x10 column of data in an array
            char yoffset =  digit * 5;
            for (y=0; y<5; y++){
                for (x=0; x<4; x++){
                    digitsArray[arrayIndex++]= vram[(y+yoffset)*128 + (x + gx)];
                }
            }
        }
    }

    // 2. BANK IN THE SPRITES - switch to target bank
    banksMirror = bankflip | GRAM_PAGE(bank);
    *bank_reg = banksMirror;
    
    {
        char _posx = onesDigitPosx;
        char gy = ones * 5;
        char w=4;
        char h=4;
        char x,y;
        for (y=0; y<h; y++){
            for (x=0; x<w; x++){
                char index=(y * w) + x + gy * 4;
                vram[((y + posy) * 128) + _posx+x] = digitsArray[index];
            }
        }
    }   
    
    // 4. RESTORE EVERYTHING
    flagsMirror = oldFlags;
    banksMirror = oldBanks;
    *dma_flags = flagsMirror;
    *bank_reg = banksMirror;

}
void draw_wave_number_prog1(unsigned char wave, int posx, int posy) {
    unsigned char tens = 0;
    unsigned char ones = wave;
    unsigned char shiftLeftOffset = wave / 10;

    if (wave<1) return;
    // Fast 8-bit division for numbers < 100
    while (ones >= 10) {
        ones -= 10;
        tens++;
    }

    // Draw Tens digit (only if > 0 to avoid "01", "02", etc.)
    if (tens > 0) {
        queue_draw_sprite(posx - (shiftLeftOffset * 4), posy, 4, 4, 124, tens * 5, PRESSSTARTSPRITEBANK);
        // Draw Ones digit with a 5-pixel offset
        queue_draw_sprite(posx + 4 - (shiftLeftOffset * 4), posy, 4, 4, 124, ones * 5, PRESSSTARTSPRITEBANK);
    } else {
        // Just draw the single digit
        queue_draw_sprite(posx - (shiftLeftOffset * 4), posy, 4, 4, 124, ones * 5, PRESSSTARTSPRITEBANK);
    }
}

void BakeLifeBar_prog1(char bank){
    char oldFlags = flagsMirror; // Save the original screen/DMA settings
    char oldBanks = banksMirror; 
    unsigned char x,y;

    // 1. SET THE HARDWARE MODE
    // Clear Bit 0 (DMA_ENABLE) and Clear Bit 1 (DMA_CPU_TO_VRAM)
    flagsMirror &= ~(DMA_ENABLE | DMA_CPU_TO_VRAM); 
    *dma_flags = flagsMirror;

    // 2. BANK IN THE SPRITES
    banksMirror = bankflip | GRAM_PAGE(bank);
    *bank_reg = banksMirror;

    {//draw life bar
        char y=17;
        for (x=92; x<BRICK_RIGHT; x+=2){
            vram[y*128 + x] = PADDLECOLOR;//0b10111111;
        }
    }
    // 4. RESTORE EVERYTHING
    flagsMirror = oldFlags;
    banksMirror = oldBanks;
    *dma_flags = flagsMirror;
    *bank_reg = banksMirror;
}
void draw_lives_prog1(char lives)
{
    char width = BRICK_RIGHT-92;
    lives = (lives<0) ? 0 : lives;
    queue_draw_box(92,16,width,2,0);//erase line
    if (lives > 0) {
        //EraseRect(92,16,lives << 1,2);//paints all lives
        queue_draw_sprite(92,16,lives << 1,2,92,16,PADDLESPRITEBANK);//maximum 16 lives visible
    }
}
void loading_screen_prog1(){
    //print out loading message
    queue_clear_border(BGCOLOR);//color
    queue_draw_sprite(32,64,64,5,0,54,PRESSSTARTSPRITEBANK);//press start  
    await_draw_queue();
    await_vsync(1);
    flip_pages();
    queue_clear_border(BGCOLOR);//color
    queue_draw_sprite(32,64,64,5,0,54,PRESSSTARTSPRITEBANK);//press start  
    await_draw_queue();
    await_vsync(1);
    flip_pages();
}
void reset_reflect_angles_prog1(){
    int i = 0;
    
    for (i = 0; i < NUM_ANGLES; i++){
        currentReflectAngles_prog1[i] = reflectAnglesNew_prog1[i];
    }
}

#pragma code-name (pop);
void EraseRect(char x, char y, char w, char h) {
    queue_draw_sprite(x, y, w, h,x,y, BAKEDBACKGROUNDSPRITEBANK); 
}

bool isSkipAhead = false;
void GameWrapperStateMachine()
{
    bool buttonCPress;
    bool buttonAPress;
    bool buttonBPress;
    bool buttonStartPress;
    
    eraseBlinkMessage(&messageEraseCount);
    button_byte = get_paddle_rotation(player1_buttons);//gets paddle input byte

    change_rom_bank(BANK_PROG1);
    erase_paddle_prog1();
    //sample inputs first
    buttonAPress = (player1_buttons & INPUT_MASK_PADDLE_A) && (~player1_old_buttons & INPUT_MASK_PADDLE_A);
    buttonBPress = (player1_buttons & INPUT_MASK_PADDLE_B) && (~player1_old_buttons & INPUT_MASK_PADDLE_B);
    buttonCPress = (player1_buttons & INPUT_MASK_PADDLE_C) && (~player1_old_buttons & INPUT_MASK_PADDLE_C);
    buttonStartPress = player1_buttons & INPUT_MASK_PADDLE_START && ~player1_old_buttons & INPUT_MASK_PADDLE_START;

    BreakoutBGRefresh_prog1();//always need the black background and borders

    switch (gameState) {
        case STATE_CONTROLLER_SELECT:
        change_rom_bank(BANK_PROG2);
        SelectControllerScreen_prog2(buttonAPress,buttonBPress);
        change_rom_bank(BANK_PROG1);
        break;
        
        case STATE_ATTRACT:
            change_rom_bank(BANK_PROG2);
            update_paddle_offset_prog2();//for paddle automation only
        case STATE_BREAKOUT_PLAY:
            if ((gameState != STATE_ATTRACT) && (buttonAPress || buttonStartPress)){
                if (spawnTimer>3 && (get_active_ball_count_prog1()<=0) && !isSkipAhead){
                    spawnTimer = 2;//shortens current timers
                    isSkipAhead = true;//fixes ball serve timer
                }                
            }
            change_rom_bank(BANK_PROG1);
            DrawBricksWrapper_prog1();//blits
            //draw_wave_number_prog1(cycleLevel, BRICK_RIGHT - 4, 114);
            if (subState != SUBSTATE_NEW_LEVEL) process_all_balls_prog1();//not needed for new level but no harm
            erase_balls_prog1();//blits
            if (gameState == STATE_ATTRACT && (subState!=SUBSTATE_NEW_LEVEL)) printBlinkMessage_prog1(0);//message before balls
            render_balls_prog1();//blits
            update_player_paddle_prog1();//cpu
            render_paddle_prog1();//blits

            switch (subState){
                case SUBSTATE_NEW_LEVEL:
                if (newLevelTimer>0){
                    newLevelTimer--;
                }
                else{
                    GenNextLevel_prog1(cycleLevel++);//unbound intentionally
                    bake_wave_number_prog1(cycleLevel, BRICK_RIGHT - 4, 113,BAKEDBACKGROUNDSPRITEBANK);
                    newLevelTimer=100;
                    NewLevel_prog1();
                    subState=SUBSTATE_PLAY;
                    if (isSkipAhead) spawnTimer = 2;
                    else spawnTimer = 100;
                    isSkipAhead = false;
                }
                break;
                case SUBSTATE_PLAY://ball(s) are in play                
                    #if TESTFEATURES
                    if (buttonCPress){
                        subState = SUBSTATE_NEW_LEVEL;
                        messageEraseCount=2;
                    }
                    #endif
                    if (bricks_remaining<=0) {
                        newLevelTimer=100;
                        subState = SUBSTATE_NEW_LEVEL;
                        if (get_active_ball_count_prog1()>=2){
                            addLives(1);//extra life!
                            //playNoteSequence(1);
                        }
                    } 
                    else if (get_active_ball_count_prog1()<=0) //no balls in play
                    {
                        if (lives_left == 0) 
                        {
                            gameState = STATE_BREAKOUT_GAMEOVER;
                            gameOverTimer = 500;
                        }
                        else if ( spawnTimer > 0){
                            if (gameState != STATE_ATTRACT) {//attract mode never says get ready
                                if (spawnTimer < 3){
                                    printGetReady_prog1(32,64,false);
                                } else {
                                    printGetReady_prog1(32,64,true);
                                }
                            }
                            spawnTimer--;
                        }
                        else//the ball just went out of bounds
                        {
                            subState = SUBSTATE_NEW_BALL; //player serves ball or it auto-spawns
                            if (gameState != STATE_ATTRACT) {
                                if (isSkipAhead) spawnTimer = 2;
                                else spawnTimer = 100;
                                isSkipAhead = false;
                            }
                        }
                    }
                    if ((gameState == STATE_ATTRACT) && (buttonStartPress || buttonAPress)){
                        init_game_prog1();
                        gameState = STATE_BREAKOUT_PLAY;
                        subState = SUBSTATE_NEW_LEVEL;
                        messageEraseCount=2;
                        playNoteSequence(1);//ascending
                    }
                //}
                break;
                case SUBSTATE_NEW_BALL://no ball in play, delay, spawn new ball
                    if (lives_left>0 && spawnTimer>0){//timer for ball spawn
                            if (spawnTimer < 3){
                                printGetReady_prog1(32,64,false);
                            } else {
                                printGetReady_prog1(32,64,true);
                            }
                            spawnTimer--;                    
                    }
                    else{
                        spawnTimer = 100;//reset for next serve, is necessary
                        isSkipAhead = false;//just in case
                        spawn_new_rnd_ball_prog1();
                        subState=SUBSTATE_PLAY;
                    }
                //}
                break;
            }
        break;

        case STATE_BREAKOUT_GAMEOVER:
        gameOverTimer--;
        if (gameOverTimer==0 || ((gameOverTimer < 400) && buttonAPress)){
            gameState = STATE_ATTRACT;
            messageEraseCount=2;
            pop_rom_bank();
            save_hiscore(hiscore);
            change_rom_bank(BANK_PROG1);
            init_game_prog1();
        }
        else
        {
            printBlinkMessage_prog1(1);
        }
        break;
    }
    paddleX_old_old = paddleX_old;
    paddleX_old = paddleX;
}


#define SCOREPOSX 6//5
#define HISCOREPOSX 52//50//(64 - ((7 * 4)/2)) //46
#define SCOREWIDTH 36

//emulator checks
uint8_t emuStatus;
uint8_t lazyDevFrameCounter=0;

void main () {
    emuStatus = CHK_EMU;
    if ((emuStatus & 0b11010101) == 0x55) {//2=paddle connected, 8=paddle emulation enabled
        isEmulator = true;
    }
    #if DEMOMODE
        if (emuStatus & 0x20){//not dks paddle connected
            demoModeUnlock = true; //only checks once
        }
    #endif
    init_music();
    load_spritesheet(ASSET__gfx__paddletank_paddle_bmp,PADDLESPRITEBANK);
    load_spritesheet(ASSET__gfx__PressStart_bmp,PRESSSTARTSPRITEBANK);
    load_spritesheet(ASSET__gfx__TitleScreen_bmp,TITLESPRITEBANK);
    
    load_hiscore();
    scoring_init();
    change_rom_bank(BANK_PROG1);
    BakeBreakoutBackground_prog1(BAKEDBACKGROUNDSPRITEBANK);//bank 1
    BakeLifeBar_prog1(PADDLESPRITEBANK);
    loading_screen_prog1();

    bake_gradient_mask_prog1();//only needs to be done once
    init_game_prog1();
    init_ball_system_prog1();
    bake_animation_frames_v2_prog1();//only generate errosion vault once
    
    //clunky version number
    bake_num_prog1(VERSION_MAJOR,BRICK_RIGHT - 35,110,TITLESPRITEBANK);
    bake_num_prog1(10,BRICK_RIGHT - 30,110,TITLESPRITEBANK);
    bake_num_prog1(VERSION_MINOR/10,BRICK_RIGHT - 25,110,TITLESPRITEBANK);
    bake_num_prog1(VERSION_MINOR%10,BRICK_RIGHT - 20,110,TITLESPRITEBANK);
    bake_num_prog1(10,BRICK_RIGHT - 15,110,TITLESPRITEBANK);
    bake_num_prog1(VERSION_PATCH / 10,BRICK_RIGHT - 10,110,TITLESPRITEBANK);
    bake_num_prog1(VERSION_PATCH % 10,BRICK_RIGHT - 5,110,TITLESPRITEBANK);

    //BreakoutBricksSetupWrapper_prog1(BRICKSSPRITEBANK); //redundant but keeps vram properly setup
    GenNextLevel_prog1(0);//sets up the first level in advance
    gameState = STATE_CONTROLLER_SELECT;

    
    while (1) 
    {                                     //  Run forever
        lazyDevFrameCounter++;
        if (isEmulator && (lazyDevFrameCounter & 0x3F)){
            CHECK_PADDLE_STATE();
        }
        #if DEMOMODE
        if (gameState!=STATE_ATTRACT){//life limit not applied to attract mode
            if (!demoModeUnlock){ //not dks paddle connected
                if (cycleLevel>3){//if level 3 is completed
                    if (demoModeFrames++==2){//give at least 2 frames to refresh
                        if (lives_left>0) {
                            lives_left=0;//die asap
                        }
                    }
                }
            }
        }
        #endif
        //GameWrapper();
        GameWrapperStateMachine();
        queue_clear_border(BGCOLOR);//2//color

        if (gameState != STATE_CONTROLLER_SELECT){
            if (scoreRefreshFrames)EraseRect(SCOREPOSX-1,10,SCOREWIDTH,9);//clear scores from previous frames
            if (hiscoreRefreshFrames)EraseRect(HISCOREPOSX-1,10,SCOREWIDTH,9);//clear scores from previous frames
            if (livesRefreshFrames)draw_lives_prog1(lives_left);
            livesRefreshFrames--;
            await_draw_queue();

            if (scoreRefreshFrames)print_scores(score,SCOREPOSX);
            scoreRefreshFrames--;

            if (hiscoreRefreshFrames)print_scores(hiscore,HISCOREPOSX);
            hiscoreRefreshFrames--;
        }

        await_vsync(1);
        flip_pages(); 
        tick_music();
        //update_inputs();
        update_paddle_inputs();//new inputs for paddle feature
    }
    
}