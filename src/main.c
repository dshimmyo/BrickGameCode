#include "gt/gametank.h"
#include "gt/gfx/draw_queue.h"
#include "gen/assets/audio.h"
#include "gt/audio/music.h"
#include "gt/input.h"
#include "gt/feature/random/random.h"
#include "scoring.h"
#include "gen/assets/gfx.h"
#include "gt/feature/paddle/paddle.h" 
#include "gt/banking.h" //Banking API
#include "gen/bank_nums.h" //Generated macros for named bank numbers
#include "gt/feature/text/text.h"
#include "gen/assets/font.h"
#include "gen/assets/sdk_default.h"

#define BOXCOLOR WHITE//92
#define BOXCOLORA WHITE
#define PADDLECOLOR WHITE
#define PADDLEWIDTH 16
#define PADDLEHEIGHT 4
#define PADDLEY 108
#define BALLSIZE 2
#define BALLCENTEROFFSET ((BALLSIZE>2) ? BALLSIZE>>1 : 0)
#define BINARYTESTPOSY 10

#define SPRITEBANKBAKEDBACKGROUND 1
#define SPRITEBANKBRICKS 2//3
#define SPRITEBANKPRESSSTART 3//4
#define SPRITEBANKPADDLE 4//5

#define LOADSPRITEBG false//test loading a sprite in the bg

#define TESTFEATURES true

//brick presets
//8cols 15 pixels wide 
//10cols 12 pixels wide 
//12cols 10 pixels wide
//15cols 8 pixels wide
//20cols 6 pixels wide
//24cols 5 pixels wide
//30cols 4 pixels wide
//60cols 2 pixels wide
//#define BRICK_WIDTH 2// 12// 2// 4// 8//12//2//8//12

#define BRICK_HEIGHT 4
#define MAX_BRICK_COLS 60
//#define NUM_BRICK_COLS 60//10//60//30//15//30//60//15 //10
#define NUM_BRICK_ROWS 6

//dynamic brick layout
unsigned char currentBrickWidth = 120;//12;//8;//4;//2;
unsigned int currentNumBricksH = 1;//10;//15;//30;//60;

#define CEILING 21

#define BRICK_TOP    35//39//25 + 18
#define BRICK_BOTTOM 58//58//44 + 18
#define BRICK_LEFT   4//6//4//4 pixels border but maybe only 3 visible
#define BRICK_RIGHT  123//123//4 pixels border
#define BRICK_ANIMATION true

#define MAX_NOISE 64 //noise samples for dissolve animations

#define CONTROLLER_STYLE_PADDLE0 0
#define CONTROLLER_STYLE_GPAD1 1
#define MAX_BALLS 4 // let's be realistic, 2 is enough

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

char controller_style = 0;
unsigned char lives_left = 3;
char breakoutRefresh=0;//set to true to refresh the whole screen
char breakoutBricksRefresh=0;//set to true to refresh the whole screen
unsigned int bricks_remaining = 0;
char messageEraseCount = 0;//set to 2 or 3 to erase messages for 2 or 3 frames

//typedefs
typedef struct {
    int dx;
    int dy;
} angle;

typedef struct {
    bool visible[MAX_BRICK_COLS];//allocate max number of bricks
    char erase_frames[MAX_BRICK_COLS];
} EntityRow;

// A small structure to track pending erases
typedef struct {
    int brick_index;
    int frames_left;
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




//prototypes
void check_brick_collision_prog1(char *,char *, int *, int *, int *);
//void randomizeBox_prog1(char *_box_x, char *_box_y, int *_dx, int *_dy, int *_ballSpeed);
int speedMult_prog1(int source, int speed);
void BrickHeal_prog1(unsigned char, unsigned char, unsigned char);
void process_anim_queue_prog1();
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

#define NUM_ANGLES 22
#pragma rodata-name (push, "PROG1")
const angle reflectAnglesNew_prog1[NUM_ANGLES]=
{{-251<<1,-30<<1},{-247<<1,-61<<1},{-235<<1,-86<<1},{-224<<1,-112<<1},{-202<<1,-146<<1},{-181<<1,-181<<1},{-146<<1,-202<<1},{-112<<1,-224<<1},{-86<<1,-235<<1},{-61<<1,-247<<1},{-30<<1,-251<<1},
{30<<1,-251<<1},{61<<1,-247<<1},{86<<1,-235<<1},{112<<1,-224<<1},{146<<1,-202<<1},{181<<1,-181<<1},{202<<1,-146<<1},{224<<1,-112<<1},{235<<1,-86<<1},{247<<1,-61<<1},{251<<1,-30<<1}};
#pragma rodata-name (pop)

unsigned char brickRowPoints[6]={4,3,2,2,1,1};//number of points gained by hitting a brick in each row, top to bottom

EntityRow brickRows[NUM_BRICK_ROWS];//init in NewLevel()

#define ERASE_QUEUE_MAX 8 //eight is enough
EraseTask erase_queue[ERASE_QUEUE_MAX];
unsigned char erase_queue_count = 0;

#define NUM_ANIM_FRAMES 4 //8 a little sluggish
#define ANIM_QUEUE_MAX 4
EraseTask anim_queue[ANIM_QUEUE_MAX];//[NUM_ANIM_FRAMES*2*MAX_BALLS];//10 frames of animation over maybe double frames for up to two balls animframes * 2X * maxballs
unsigned char  anim_queue_count = 0;

unsigned char animatedBricks[MAX_BRICK_COLS*NUM_BRICK_ROWS]; //this needs to be re-engineered

#define NUM_BRICK_LAYOUTS 16
BrickLayout brickLayouts[NUM_BRICK_LAYOUTS]=
{
    //{cols,width}
    {1,120},
    {2,60},
    {3,40},
    {4,30},
    {5,24},
    {6,20},
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

bool demoMode = false;
bool debugMode = false;

// //boxes, make more modular
// char box_x = 64, box_y = 64;
// char box_x_old = 64, box_y_old = 64;
// char box_x_old_old = 64, box_y_old_old = 64;

// char boxA_x = 64, boxA_y = 64;
// char boxA_x_old = 64, boxA_y_old = 64;
// char boxA_x_old_old = 64, boxA_y_old_old = 64;

// int ballSpeed = 1;//actual speed is in .5 increments
// int ballSpeedA = 1;

// //ball movement variables
// //256 is 1 pixel per frame
// int dx = 256, dy = 256;//range -256 to 256
// int dxA = 256, dyA = 256;
// int dxRem=0;//remainder
// int dyRem=0;

//new ball variables:
unsigned char active_balls_count = 0;
Ball balls[MAX_BALLS];//pre-allocate enough space for MAX_BALLS

char paddleX = 64;//global paddle xpos
char paddleX_old=64;
char paddleX_old_old=64;
unsigned char button_byte=0;//8-bit byte representing button states, for debugging and potential use in game mechanics
unsigned int score = 0;//global score variable
unsigned int hiscore = 100;//global score variable
unsigned char scoreRefreshFrames = 2;
unsigned char hiscoreRefreshFrames = 2;
int resetTimer = 100;//delay between brick reset and ball reset
int rndPaddleOffset = 0;
unsigned char rndPaddleOffsetTimer = 0;

int blinkTimer=0;
char blinkMessageVisible=0;
unsigned int readyTimer=0;
unsigned char cycleLevel=0;


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
}

void soundTest(){
    play_sound_effect(ASSET__audio__flongNew_sfx_ID,1);
}
void soundOutOfBounds()
{
    play_sound_effect(ASSET__audio__outOfBounds_sfx_ID,1);
}
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
        if (reflectAnglesNew_prog1[index].dx == _dx) return index;
        if (_dx > reflectAnglesNew_prog1[index].dx) newDiff = _dx - reflectAnglesNew_prog1[index].dx;
        else newDiff = reflectAnglesNew_prog1[index].dx - _dx;
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
    const char angleAdjust = 32;//64
    const char minAngle = 96;//64
    const char maxAngle = 512 - minAngle;
    char paddleHitRegion = 0;
    char nearestIndex=0;
    angle nearestAngle;
    int tempDx = *_dx;//keep changes local until done
    int tempDy = *_dy;//keep changes local until done

    if (tempDy < 0) return false;//don't waste time on already bounced balls
    if (sourceXLow > paddleX) paddleHitRegion = (sourceXLow - paddleX)/(PADDLEWIDTH / 8);
    else paddleHitRegion = 0;
    
    if (sourceYHi < PADDLEY) return false;
    else if (sourceYLow > PADDLEY+PADDLEHEIGHT/*targetYLow + PADDLEHEIGHT*/) return false;
    else if (sourceXHi < paddleX) return false;
    else if (sourceXLow > paddleX+PADDLEWIDTH) return false;

    //if it gets to here the ball is colliding with the paddle, now determine reflection angle based on hit region

    //if the ball is in slow mode, turn it up
    *_ballSpeed = (*_ballSpeed<2) ? 2 : *_ballSpeed;
    //ballSpeedA = (ballSpeedA<2) ? 2 : ballSpeedA;

    //standard reflection
    // tempDy = (tempDy<0) ? tempDy : - ((unsigned int) tempDy);
    // tempDx = tempDx;

    //attempt to make sophisticated reflections:
    //nearestIndex = GetNearestReflectAngleIndex(tempDx);

    //22 angles
    //0  1  2  3  4  5  6  7  8  9  10
    //21 20 19 18 17 16 15 14 13 12 11
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
        case 8://extra zone? far right
        nearestIndex = 17;//20;//21
        break;

    }
    nearestAngle = reflectAnglesNew_prog1[nearestIndex];
    *_dx = nearestAngle.dx;
    *_dy = nearestAngle.dy;
    play_sound_effect(ASSET__audio__paddle_sfx_ID,(char)0);

    return true;
}
angle get_random_spawn_angles_prog1()
{
    char returnIndex;
    int indexIndex = rnd_range(8,13);
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
        case 8://extra zone? far right
        returnIndex = 17;//20;//21
        break;
    }
    returnAngle = reflectAnglesNew_prog1[returnIndex];
    return returnAngle;
}
#pragma code-name (pop)


#pragma code-name (push, "PROG1")
// int approx_div_3_prog1(int num) {
//     return (num >> 5) * 11; // This is an approximation using a fixed-point representation
// }
// int approx_div_3_prog1(int num) {
//     return (num >> 6) * 21; // This is an approximation using a fixed-point representation
// }
int approx_five_twelfths(int num){
    return (num >> 5) * 13;
}
int speedMult_prog1(int source,int mult){//multiplier can be fine-tuned here and hidden from code
    //return (source>>1) * mult; //half increments
    //return (source/3) * mult; //third increments, more work for cpu
    return approx_five_twelfths(source) * mult;
}
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
    int scaledDx = speedMult_prog1(b->dx, b->speed);
    int scaledDy = speedMult_prog1(b->dy, b->speed);
    
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
        else if(b->y >= 120 - BALLSIZE) {
            b->active = false;//despawn, stops rendering
            soundTest();
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
        rndPaddleOffset = rnd_range(0,PADDLEWIDTH>>1);
        if (rnd_range(0,127)<64) rndPaddleOffset *= -1; 
        rndPaddleOffsetTimer=0;
    }
}

#pragma code-name (pop)


#pragma code-name (push, "PROG1")
// Convert individual button states to a 8-bit byte
unsigned char buttons_to_byte_xyzm_prog1(int player1_buttons) {
    char result = 0;
    
    // This could be optimized further if you know your masks are powers of 2
    if (player1_buttons & INPUT_MASK_UP) result |= (1 << 0);
    if (player1_buttons & INPUT_MASK_DOWN) result |= (1 << 1);
    if (player1_buttons & INPUT_MASK_LEFT) result |= (1 << 2);
    if (player1_buttons & INPUT_MASK_RIGHT) result |= (1 << 3);
    if (player1_buttons & INPUT_MASK_X) result |= (1 << 4);
    if (player1_buttons & INPUT_MASK_Y) result |= (1 << 5);
    if (player1_buttons & INPUT_MASK_Z) result |= (1 << 6);
    if (player1_buttons & INPUT_MASK_MODE) result |= (1 << 7);

    return ~result;//invert result
}
char ClampPaddleX_prog1(int paddlex){
    if (paddlex < BRICK_LEFT){
        paddlex = BRICK_LEFT;
    } else if (paddlex>BRICK_RIGHT-PADDLEWIDTH){
        paddlex = BRICK_RIGHT - PADDLEWIDTH;
    }
    return paddlex;
}
int setRangeOpt_prog1(char input, char inMin, char inMax)
{
    int output = 0;
    if (input<inMin)return 0;
    else if (input>inMax)return 127;

    output = ((127<<8)/(inMax-inMin)*(input-inMin)>>8);
    return output;
    
}
char paddleXFromPot8opt_prog1(unsigned char potVal)
{
    //need to make a cheaper setrange function
    unsigned char newPotVal = setRangeOpt_prog1(potVal,0b01000000,0b11000000);//changed for 8-bit

    return ClampPaddleX_prog1(newPotVal);//(((newPotVal<<1)/3 + paddleX/3));//no smoothing
}
char paddleXFromDPad_prog1()
{
    int result = paddleX;
    if ((player1_buttons & INPUT_MASK_RIGHT)) result += 3;
    else if ((player1_buttons & INPUT_MASK_LEFT)) result -= 3;
    return ClampPaddleX_prog1(result);
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

char paddleXFromClosestBall_prog1(){
    unsigned char lowest_y = 0;
    signed char target_ball_index = -1;
    unsigned char i;
    // Find the ball closest to the paddle
    for (i = 0; i < MAX_BALLS; i++) {
        if (balls[i].active && balls[i].y > lowest_y) {
            lowest_y = balls[i].y;
            target_ball_index = i;
        }
    }
    return ConstVelocity_prog1(paddleX, balls[target_ball_index].x - (PADDLEWIDTH>>1) + rndPaddleOffset,3);//
}
void GenNextLevel_prog1(char procLevel)
{
    BrickLayout bl;
    bl = brickLayouts[procLevel % NUM_BRICK_LAYOUTS];
    currentBrickWidth = bl.brickWidth;
    currentNumBricksH = bl.numColumns;
    // change_rom_bank(BANK_PROG1);
    BreakoutBricksSetupWrapper_prog1(SPRITEBANKBRICKS);
}
unsigned char everyOtherFrame;
char paddleXFromBallArray_prog1() {
    int midPaddle = paddleX + (PADDLEWIDTH>>1);
    
    // Tracking variables for our "Best Candidates"
    char highestBallIdx = -1;
    char closestBallIdx = -1;
    char downestDyIdx = -1;
    
    unsigned char maxPosY = -1;
    unsigned char minPosY = 255;
    int maxDy = -512; // Start with lowest possible dy
    int minClosestScore = 255;
    unsigned char i;
    if ((everyOtherFrame++) & 0x00000001) return paddleX;//skip every other frame
    //int offset = (int)rndPaddleOffset; // Force the 8-bit sign to fill the 16-bit int
    for (i = 0; i < MAX_BALLS; i++) {
        int xDist;
        if (!balls[i].active) continue;//jump to end of loop body

        // 1. Find Highest Ball (Smallest Y) - useful when all are moving up
        // if (highestBallIdx<0) highestBallIdx = i;
        // if (downestDyIdx<0) downestDyIdx = i;
        // if (closestBallIdx<0) closestBallIdx = i;
        else if (balls[i].y < minPosY) {
            minPosY = balls[i].y;
            highestBallIdx = i;
        }

        // 2. Find Most Downward Ball (Largest dy)
        if (balls[i].dy > maxDy) {
            maxDy = balls[i].dy;
            downestDyIdx = i;
        }

        // 3. Find "Closest" Ball 
        // Logic: Distance to paddle + Bias for downward movement
        xDist = (balls[i].x > midPaddle) ? balls[i].x - midPaddle : midPaddle - balls[i].x;
        if (balls[i].dy > 0) {
            if (xDist < minClosestScore) {
                minClosestScore = xDist;
                closestBallIdx = i;
            }
        }
    }

    // Safety check: if no balls are active, just stay put or return 0
    if (highestBallIdx < 0) return paddleX;

    // --- Decision Logic (Refined from your original) ---
    
    // If even the "most downward" ball is moving UP, follow the highest ball 
    // (It's the most likely to bounce off a brick and come back down first)
    if (balls[downestDyIdx].dy < 0) {
        int temp = balls[highestBallIdx].x - (PADDLEWIDTH>>1) + rndPaddleOffset;
        // if (temp < 0) temp = 0;
        // if (temp > 120) temp = 120;
        return ConstVelocity_prog1((char) paddleX, temp, 6);
    } 
    // If we have a ball moving downward, prioritize the one closest to our X
    else if (closestBallIdx != -1) {
        int temp = balls[closestBallIdx].x - (PADDLEWIDTH>>1) + rndPaddleOffset;
        // if (temp < 0) temp = 0;
        // if (temp > 120) temp = 120;
        return ConstVelocity_prog1((char) paddleX, temp, 6);
    } 
    // Fallback: Just track the one with the most downward momentum
    else {
        int temp = balls[downestDyIdx].x - (PADDLEWIDTH>>1) + rndPaddleOffset;
        // if (temp < 0) temp = 0;
        // if (temp > 120) temp = 120;
        return ConstVelocity_prog1((char) paddleX, temp, 6);
    }

    // TEMP DEBUG LOGIC: Just follow the lowest active ball
    // {
    //     int targetIdx = -1;
    //     int bestY = -1;
    //     for(i=0; i<MAX_BALLS; i++) {
    //         if(balls[i].active && balls[i].y > bestY) {
    //             bestY = balls[i].y;
    //             targetIdx = i;
    //         }
    //     }

    //     if (targetIdx != -1) {
    //         int targetX = balls[targetIdx].x - halfPaddle + offset;
    //         if (targetX < 0) targetX = 0;
    //         if (targetX > 110) targetX = 110;
    //         return ConstVelocity_prog1((char)paddleX, (char)targetX, 3);    
    //     }
    // }
    // return paddleX;

}

/*
char paddleXFromClosestBox_prog1(){
    char midPaddle = paddleX + (PADDLEWIDTH>>1);
    char boxA_dist = (boxA_x > midPaddle) ? boxA_x - midPaddle : midPaddle - boxA_x;
    char box_dist = (box_x > midPaddle) ? box_x - midPaddle : midPaddle - box_x;
    int dyList[2];
    char PosXList[2];
    char PosYList[2];
    char XDistList[2];
    char selectedDyIndex=0;
    char lowestBallIndex=0;
    char highestBallIndex=0;
    char closestBallIndex=0;
    char downestDyIndex=0;
    char i=0;
    XDistList[0]=box_dist;
    XDistList[1]=boxA_dist;
    dyList[0] = dy;
    dyList[1] = dyA;
    PosXList[0] = box_x;
    PosXList[1] = boxA_x;
    PosYList[0] = box_y;
    PosYList[1] = boxA_y;
    for (i=1; i<2; i++){//find the lowest, highest, closest and most downward
        lowestBallIndex = (PosYList[i]>=PosYList[i-1]) ? i:i-1;
        highestBallIndex = (PosYList[i]>=PosYList[i-1]) ? i-1:i;
        closestBallIndex = (PosYList[i] - XDistList[i]>>1 >=PosYList[i-1] - XDistList[i-1]>>1 && dyList[i]>0) ? i:i-1;
        downestDyIndex = (dyList[i]>dyList[i-1]) ? i:i-1;
    }
    //return ConstVelocity(paddleX, PosXList[lowestBallIndex] - (PADDLEWIDTH>>1),4);//
    if (dyList[downestDyIndex] < 0 ){//both up, follow the highest, most likely to bounce
        return ConstVelocity_prog1(paddleX, PosXList[highestBallIndex] - (PADDLEWIDTH>>1) + rndPaddleOffset,3);//
    } else if (dyList[closestBallIndex]>0){//if the closest is downward
        return ConstVelocity_prog1(paddleX, PosXList[closestBallIndex] - (PADDLEWIDTH>>1) + rndPaddleOffset,3);//
    } else { //just pick the one traveling downward
        return ConstVelocity_prog1(paddleX, PosXList[downestDyIndex] - (PADDLEWIDTH>>1) + rndPaddleOffset,3);//
    }
}
*/
// void randomizeBox_prog1(char *_box_x, char *_box_y, int *_dx, int *_dy, int *_ballSpeed){
//     *_box_x = rnd_range(5,114);
//     *_box_y = rnd_range(64,68);
//     *_dx = (rnd_range(0,10) > 5) ? 255 : -255;
//     *_dy = 255;//always down
//     *_ballSpeed = 1;//default slow
// }
void NewLevel_prog1()
{
    unsigned char x;
    unsigned char y;
    int count = 0;
    //angle randomAngle;
    for (y = 0; y<NUM_BRICK_ROWS; y++){
        for (x=0; x<currentNumBricksH; x++){
            brickRows[y].visible[x] = 1;
            brickRows[y].erase_frames[x] = 0;
            //animatedBricks[count++]=0;//set all bricks to unanimated
        }
    }
    erase_queue_count=0;
    anim_queue_count=0;
    // ballSpeed = 1;//slow start
    // ballSpeedA = 1;//slow start
    bricks_remaining = currentNumBricksH * NUM_BRICK_ROWS;
    //change_rom_bank(BANK_PROG1);
    // randomizeBox_prog1(&box_x, &box_y, &dx, &dy, &ballSpeed);
    // randomizeBox_prog1(&boxA_x, &boxA_y, &dxA, &dyA, &ballSpeedA);
    // change_rom_bank(BANK_PROG1);
    init_ball_system_prog1();
    // randomAngle = get_random_spawn_angles_prog1();
    // spawn_new_ball_prog1(rnd_range(20,100),80,randomAngle.dx,-randomAngle.dy,2);
    // randomAngle = get_random_spawn_angles_prog1();
    // spawn_new_ball_prog1(rnd_range(20,100),80,randomAngle.dx,-randomAngle.dy,2);
    spawn_new_rnd_ball_prog1();
    spawn_new_rnd_ball_prog1();
    
    // randomAngle = get_random_spawn_angles_prog1();
    // spawn_new_ball_prog1(rnd_range(20,100),80,randomAngle.dx,-randomAngle.dy,2);
    // randomAngle = get_random_spawn_angles_prog1();
    // spawn_new_ball_prog1(rnd_range(20,100),80,randomAngle.dx,-randomAngle.dy,2);
    breakoutRefresh=0;
    breakoutBricksRefresh=0;
    scoreRefreshFrames=2;
    hiscoreRefreshFrames=2;
}
#pragma code-name (pop)


#pragma code-name (push, "PROG2")
void inputButtonsDraw_prog2()
{
    //input testing
    if (player1_buttons & INPUT_MASK_UP){queue_draw_box(1,7,8,2,182);}
    if (player1_buttons & INPUT_MASK_DOWN){queue_draw_box(11,7,8,2,182);}
    if (player1_buttons & INPUT_MASK_LEFT){queue_draw_box(21,7,8,2,182);}
    if (player1_buttons & INPUT_MASK_RIGHT){queue_draw_box(31,7,8,2,182);}
    if (player1_buttons & INPUT_MASK_A){queue_draw_box(41,7,8,2,182);}
    if (player1_buttons & INPUT_MASK_B){queue_draw_box(51,7,8,2,182);}
    if (player1_buttons & INPUT_MASK_C){queue_draw_box(61,7,8,2,182);}
    if (player1_buttons & INPUT_MASK_START){queue_draw_box(71,7,8,2,182);}
    if (player1_buttons & INPUT_MASK_X){queue_draw_box(81,7,8,2,199);}//x
    if (player1_buttons & INPUT_MASK_Y){queue_draw_box(91,7,8,2,199);}//y
    if (player1_buttons & INPUT_MASK_Z){queue_draw_box(101,7,8,2,199);}//z
    if (player1_buttons & INPUT_MASK_MODE){queue_draw_box(111,7,8,2,199);}//mode

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
    resetTimer = 100;
    score = 0;
    hiscore = (hiscore<100) ? 100 : hiscore;//default 100
    subState = SUBSTATE_NEW_LEVEL;
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
                queue_draw_sprite(xDrawStart,posy,brickDrawWidth,BRICK_HEIGHT,xDrawStart,(row*BRICK_HEIGHT)/*+60*/,SPRITEBANKBRICKS);

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
                        queue_draw_sprite(BRICK_LEFT + col * currentBrickWidth,BRICK_TOP + BRICK_HEIGHT * row,currentBrickWidth,BRICK_HEIGHT,BRICK_LEFT + col * currentBrickWidth,row*BRICK_HEIGHT+offset,SPRITEBANKBRICKS);//paint box
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
        //idx = erase_queue[i].brick_index;
        //row = erase_queue[i].row;//idx / currentNumBricksH;
        //col = erase_queue[i].col;//idx % currentNumBricksH;
        posy = erase_queue[i].y;//BRICK_TOP + BRICK_HEIGHT * row;//brickRow[y].posy;//brickRowYPos[y];

        // Draw the "hole" in the current back-buffer
        //queue_draw_box(erase_queue[i].x,posy,erase_queue[i].w,erase_queue[i].h,0);//paint box
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
        DrawBricksErase_prog1();
        #if BRICK_ANIMATION
            process_anim_queue_prog1();
        #endif
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

        // Destroy brick
        brickRows[row].visible[col] = 0;
        brickRows[row].erase_frames[col] = 2;
        //animatedBricks[row*currentNumBricksH + col] = 20;//start animation for this brick //animate at half speed
        if (erase_queue_count < ERASE_QUEUE_MAX) {
            //erase_queue[erase_queue_count].brick_index = row*currentNumBricksH + col;
            erase_queue[erase_queue_count].frames_left = 2; // Two buffers to clean
            // erase_queue[erase_queue_count].row = row;
            // erase_queue[erase_queue_count].col = col;
            erase_queue[erase_queue_count].x = BRICK_LEFT + (col * currentBrickWidth);
            erase_queue[erase_queue_count].y = BRICK_TOP + (row * BRICK_HEIGHT);
            erase_queue[erase_queue_count].w = currentBrickWidth;
            erase_queue[erase_queue_count].h = BRICK_HEIGHT;
            erase_queue_count++;
        }
        if (anim_queue_count < ANIM_QUEUE_MAX)
        {
            //anim_queue[anim_queue_count].brick_index = row*currentNumBricksH + col;
            anim_queue[anim_queue_count].frames_left = NUM_ANIM_FRAMES*2+2;//double buffers?
            anim_queue[anim_queue_count].x = BRICK_LEFT + (col * currentBrickWidth);
            anim_queue[anim_queue_count].y = BRICK_TOP + (row * BRICK_HEIGHT);
            anim_queue[anim_queue_count].w = currentBrickWidth;
            anim_queue[anim_queue_count].h = BRICK_HEIGHT;
            anim_queue_count++;
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

#pragma code-name (push, "PROG1")
void BreakoutBGRefresh_prog1()
{
    if (breakoutRefresh<2){
        EraseRect(1,7,127,120);//replace the screen with pre-rendered sprite
        breakoutRefresh++;//runs twice to cover two buffers
    }
    else//clean up edges
    {
        //queue_draw_box(2,120-(BALLSIZE+2),124,BALLSIZE+2,0);//floor bar - fixes out-of-bounds ball ghosts
        EraseRect(2,120-(BALLSIZE+2),124,BALLSIZE+2);
        //queue_draw_box(2,(CEILING-2),124,2,0b01111000);//ceiling bar - fixes ball ghosts
        EraseRect(2,(CEILING-2),124,2);
        //queue_draw_box(0,7,BRICK_LEFT,127,0b01111000);//left bar - fix ball ghosts
        EraseRect(0,7,BRICK_LEFT,127);
        //queue_draw_box(BRICK_RIGHT+1,7,3,113,0b01111000);//rt bar - fix ball ghosts
        EraseRect(BRICK_RIGHT+1,7,3,113);

    }
}
void update_player_paddle_prog1(){
    if (demoMode || gameState==STATE_ATTRACT){
        paddleX = paddleXFromBallArray_prog1();
    } else {
        if (controller_style == CONTROLLER_STYLE_PADDLE0){
            paddleX = paddleXFromPot8opt_prog1(button_byte);
        }
        else if (controller_style == CONTROLLER_STYLE_GPAD1)
        {
            paddleX = paddleXFromDPad_prog1();
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
            return; 
        }
    }
}
void printPressStart_prog1(char x, char y, bool visible){
    if (visible){
        queue_draw_sprite(x,y,64,5,0,21,SPRITEBANKPRESSSTART);//press start
    } else {
        //queue_draw_box(x,y,64,5,0);//press start heal
        //queue_draw_sprite(x,y,64,5,x,y,BAKEDBACKGROUNDSPRITEBANK);//press start
        EraseRect(x,y,64,5);
    }
}
void printGameOver_prog1(char x, char y, bool visible){
    if (visible){
        queue_draw_sprite(x,y,64,5,0,40,SPRITEBANKPRESSSTART);//press start
    }else {
        //queue_draw_box(x,y,64,5,0);//press start heal
        //queue_draw_sprite(x,y,64,5,x,y,BAKEDBACKGROUNDSPRITEBANK);//press start
        EraseRect(x,y,64,5);

    }
}
void printBlinkMessage_prog1(char index){
    //0 press start
    //1 game over
    if (blinkTimer++>0)
    {
        //queue_draw_sprite(38,64,52,6,0,14,4);//press start
        if (index==0) printPressStart_prog1(32,64,true);
        else printGameOver_prog1(32,64,true);
        blinkMessageVisible=2;
        blinkTimer = (blinkTimer>45) ? blinkTimer=-80 : blinkTimer;
    }
    else
    {
        if (blinkMessageVisible) {
            messageEraseCount=2;
            //printPressStart_prog1(32,64,false);//prints a blank box
            //blinkMessageVisible--;
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

/*void BreakoutGameWrapper(){
    bool buttonCPress;
    bool buttonAPress;
    bool buttonStartPress;
    //sample inputs first
    buttonAPress = (player1_buttons & INPUT_MASK_A) && (~player1_old_buttons & INPUT_MASK_A);
    buttonCPress = (player1_buttons & INPUT_MASK_C) && !(player1_old_buttons & INPUT_MASK_C);
    buttonStartPress = player1_buttons & INPUT_MASK_START && ~player1_old_buttons & INPUT_MASK_START;
    if (buttonStartPress)//toggle demo mode automated/player modes
    {
        demoMode = !demoMode;
        if (!demoMode) messageEraseCount+=2;
    }
    change_rom_bank(BANK_PROG1);
    button_byte = buttons_to_byte_xyzm_prog1(player1_buttons);//gets paddle input

    BreakoutBGRefresh_prog1();

    if (gameState==STATE_BREAKOUT_READY)
    {
        readyTimer--;
        if (readyTimer <= 0)
        {
            spawn_new_rnd_ball_prog1();
            gameState=STATE_BREAKOUT_PLAY;
            readyTimer=100;
        }
    }
    //cover up previous paddle position
    erase_paddle_prog1();

    if ((bricks_remaining<=0) || gameState==STATE_BREAKOUT_MENU) {
        if (resetTimer--==100){
            //gameState=-1;//stopsGameplay
        }
        if (!resetTimer) {
            GenNextLevel_prog1(cycleLevel++ % NUM_BRICK_LAYOUTS);

            resetTimer=100;
            NewLevel_prog1();
            gameState=STATE_BREAKOUT_PLAY;
        }
    }
    else if (buttonCPress)//total hack for testing
    {
        gameState=-1;
        GenNextLevel_prog1(cycleLevel++ % NUM_BRICK_LAYOUTS);
        resetTimer=100;
        NewLevel_prog1();
        gameState=STATE_BREAKOUT_PLAY;
    }


    if (buttonAPress) //toggles debug mode
    {
        debugMode = !debugMode;
    }
    if (debugMode){//old legacy code for testing
        change_rom_bank(BANK_PROG2);
        inputButtonsDraw_prog2();//debug display
        inputBinaryDraw_prog2();//debug line
        change_rom_bank(BANK_PROG1);//switch back to prog1
    }
    DrawBricksWrapper_prog1();//handles full-screen refresh and optimized rendering

    if (gameState==STATE_BREAKOUT_PLAY || gameState == STATE_BREAKOUT_MENU){//menu should rename to attract
        process_all_balls_prog1();
    }

    update_player_paddle_prog1();
    render_paddle_prog1();
    erase_balls_prog1();

    if (gameState==STATE_BREAKOUT_PLAY){
        render_balls_prog1();
        if (demoMode){//hack "press start"
            printBlinkMessage_prog1(0);
        }
    } 
    else if (gameState == STATE_ATTRACT){
        printBlinkMessage_prog1(0);
        render_balls_prog1();
    }
    else if (gameState == STATE_BREAKOUT_GAMEOVER){
        printBlinkMessage_prog1(1);
    }

    paddleX_old_old = paddleX_old;
    paddleX_old = paddleX;

    if ((gameState == STATE_BREAKOUT_PLAY))//while game is still in play check gameover or ready
    {
        if (get_active_ball_count_prog1()<=0) //no balls in play
        {
            if (lives_left == 0) 
            {
                // This is where you decide: New Life or Game Over?
                gameState = STATE_BREAKOUT_GAMEOVER;
            }
            else
            {
                lives_left--;
                gameState = STATE_BREAKOUT_READY; //player serves ball or it auto-spawns
                readyTimer=100;
            }

        }
    }


}*/

#pragma code-name (push, "PROG2")
void SelectControllerScreen_prog2(bool buttonAPress, bool buttonBPress)
{
    char top = 60;
    await_draw_queue();

    text_init();
    text_color = TEXT_COLOR_WHITE;
    text_cursor_x = 10;
    text_cursor_y = top;
    text_print_string("Press A");
    text_cursor_x = 30;
    text_cursor_y = 10 + top;

    text_print_string("for Paddle");

    text_cursor_x = 10;
    text_cursor_y = 30 + top;

    text_print_string("Press B");
    text_cursor_x = 30;
    text_cursor_y = 40 + top;

    text_print_string("for Gamepad");
    if (buttonBPress) 
    {
        controller_style = CONTROLLER_STYLE_GPAD1;
        gameState = STATE_ATTRACT;
        breakoutRefresh=0;//triggers a bg refresh
        breakoutBricksRefresh=0;//triggers bricks refresh
    }
    else if (buttonAPress) 
    {
        controller_style = CONTROLLER_STYLE_PADDLE0;
        gameState = STATE_ATTRACT;
        breakoutRefresh=0;//triggers a bg refresh
        breakoutBricksRefresh=0;//triggers bricks refresh
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

void bake_animation_frames_prog1() 
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
    banksMirror = bankflip | GRAM_PAGE(SPRITEBANKBRICKS);
    *bank_reg = banksMirror;

    //copy pixels from top of vram area, every pixel for 128 x 24 pixels
    //write out anim frames in chunks of current_brick_width x 24 pixels
    //change threshold for each frame
    //maximum of 8 anim frames to accomodate 15 pixel wide bricks
    //if I want more anim frames can accomodate at most 12-pixel-wide bricks for 10 frames
    for (y=0; y<24; y++)//all 6 rows in one shot
    {
        char numFrames = (NUM_ANIM_FRAMES<=120/currentBrickWidth) ? NUM_ANIM_FRAMES : 120/currentBrickWidth;//limit to the 120 frame buffer
        char frame=0;
        for (frame=0; frame<numFrames; frame++)
        {//for every frame offset by z*currentBrickWidth
            unsigned int frameOffset = frame * currentBrickWidth;
            for (x = 0; x < currentBrickWidth; x++)//only write out frames the size of the bricks
            {
                unsigned int origCoord = (y * 128) + x + frameOffset; //only needs to be the first frame over and over
                unsigned int targetCoord = animOffset + origCoord;
                unsigned int pixel = vram[origCoord];
                //unsigned char noise = dissolve_mask[x+((y&0b00000011)*currentBrickWidth)] ;//no offset
                unsigned char noise = dissolve_mask[(x+(y*currentBrickWidth))&63] ;//no offset
                unsigned int threshold = (frame * 255/numFrames);
                // if (numFrames - frame <= 2) {
                //     vram[targetCoord] = 0; // Erase the pixel
                // } 
                // else 
                {
                    // Check against your 'Master Gradient' mask
                    if (noise < threshold) {
                        vram[targetCoord] = 0; // Erase the pixel
                    }
                    else {
                        vram[targetCoord] = pixel;
                    }
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

void process_anim_queue_prog1() {
    int i;
    unsigned int animOffsetY = 24;
    unsigned int animOffsetX;
    char animFrame;
    unsigned int animFrameMaxPixel;
    for (i = 0; i < anim_queue_count; i++) {
        EraseTask *task = &anim_queue[i];
        
        // 1. Only update the SOURCE pixels in Bank 3 once per logic frame
        // This 'eats' the brick further in the master spritesheet
        //animate_dissolve_prog1(task->col, task->row, task->frames_left);

        // 3. Draw the resulting sprite to the screen
        //queue_draw_box(task->x,task->y,task->w,task->h,0);
        EraseRect(task->x,task->y,task->w,task->h);

        animFrame = NUM_ANIM_FRAMES - (task->frames_left/2-2);
        //queue_draw_sprite(BRICK_LEFT + task->col * currentBrickWidth,BRICK_TOP + task->row * BRICK_HEIGHT,currentBrickWidth,BRICK_HEIGHT,task->col * currentBrickWidth, task->row * BRICK_HEIGHT,3);//brick sprite has no y offset
        animFrameMaxPixel = (animFrame+1) * currentBrickWidth;
        if (animFrameMaxPixel>>7) //animFrame = 128 / currentBrickWidth;//hopefully eliminate overflow noise
        {
            //sprite overflow draw nothing
        }
        else
        {
            animOffsetX = (animFrame) * currentBrickWidth;
            //queue_draw_sprite(BRICK_LEFT + task->col * currentBrickWidth,BRICK_TOP + task->row * BRICK_HEIGHT,currentBrickWidth,BRICK_HEIGHT, animOffsetX, (task->row * BRICK_HEIGHT) + animOffsetY,3);//brick sprite has no y offset
            queue_draw_sprite(task->x,task->y,task->w,task->h, animOffsetX, (task->y) + animOffsetY - BRICK_TOP,SPRITEBANKBRICKS);//brick sprite has no y offset
        
        }
        // 4. Tick down
        task->frames_left--;

        // 5. Cleanup: If 0, remove from queue (standard swap-and-pop)
        if (task->frames_left == 0) {
            anim_queue[i] = anim_queue[anim_queue_count - 1];
            anim_queue_count--;
            i--; // Re-check this index since we swapped
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
    banksMirror = bankflip | GRAM_PAGE(SPRITEBANKBRICKS);
    *bank_reg = banksMirror;

    // 3. THE PAINT LOOP (128x24 block)
    for (row = 0; row < 6; row++) {
        int row_start = 0x000 + (row * 4 * 128);//was 0x181
        int pixel = vram[row_start];//color is constant in the entire row
        for (y = 0; y < 4; y++) {//loop 4 lines of each row
            for (col = 4; col < 124; col += currentBrickWidth) {//leap from brick to brick horizontally
                int pos = row_start + (y * 128) + col;
                if (pixel != 0) {
                    vram[pos] = ((pixel & 0b00000111)<7) ? pixel + 1 : pixel;     // Highlight
                    vram[pos + currentBrickWidth-1] = ((pixel & 0b00000111)>0) ? pixel - 1 : pixel; // Shadow
                }
            }
            //top and bottom shading overrides vertical
            if (y==0){
                for (x=0; x<128;x++){
                    vram[row_start + (y * 128) + x] = ((pixel & 0b00000111)<7) ? pixel + 1 : pixel; //hilite
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
    load_spritesheet(ASSET__gfx__BreakoutBrickWide_bmp,bank);
    bevel_all_bricks_prog1();
    bake_animation_frames_prog1();
}
#pragma code-name (pop);


#pragma code-name (push, "PROG1")
void process_all_balls_prog1() {
    unsigned char i; 
    active_balls_count=0;
    for (i = 0; i < MAX_BALLS; i++) {
        if (balls[i].active) {
            update_ball_logic_prog1(&balls[i]);
            active_balls_count++;
        }
    }

    // Transition to Game Over if the wave is dead
    // if (lives_left == 0 && gameState == STATE_BREAKOUT_PLAY) {
    //     gameState = STATE_BREAKOUT_GAMEOVER;
    // }
}
void render_balls_prog1() {
    unsigned char i;
    for (i = 0; i < MAX_BALLS; i++) {
        if (balls[i].active) {
            // Only draw the ones currently in play
            //draw_sprite(balls[i].x, balls[i].y, BALL_SPRITE_ID);
            queue_draw_sprite(balls[i].x, balls[i].y,BALLSIZE,BALLSIZE,4,0,SPRITEBANKPADDLE);//hacky use of paddle sprite bank
        }
    }
}
void render_paddle_prog1() 
{
    queue_draw_sprite(paddleX,PADDLEY,PADDLEWIDTH,PADDLEHEIGHT,0,0,SPRITEBANKPADDLE);
}
void erase_paddle_prog1(){
    //queue_draw_box(paddleX_old_old,PADDLEY,PADDLEWIDTH,PADDLEHEIGHT,0);//erase paddle
    EraseRect(paddleX_old_old,PADDLEY,PADDLEWIDTH,PADDLEHEIGHT);
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
    char yOffset = 0;

    char livesArray[17*4];
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
    //queue_clear_screen(0);
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

        //for (y=posy-h+1+yOffset;y<posy+1+yOffset;y++)
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
    //load spritebank to sample lives
    banksMirror = bankflip | GRAM_PAGE(SPRITEBANKPRESSSTART);
    *bank_reg = banksMirror;
    {
        char w=17;
        char h=4;
        char xoffset = 25;
        char yoffset = 96;
        for (y=0; y<h; y++){
            for (x=0; x<w; x++){
               livesArray[y*w+x]= vram[(y+yoffset)*128 + (x + xoffset)];
            }
        }
    }
    // 2. BANK IN THE SPRITES
    banksMirror = bankflip | GRAM_PAGE(bank);
    *bank_reg = banksMirror;
    
    {
        char vramIndex=0;
        char posx = 92;
        char posy = 11;
        char w=17;
        char h=4;
        for (y=0; y<h; y++){
            for (x=0; x<w; x++){
                char index=(y * w) + x;
                vram[((y + posy) * 128) + posx+x] = livesArray[index];
            }
        }
    }
    {
        char y=17;
        for (x=92; x<BRICK_RIGHT; x+=2){
            vram[y*128 + x] = 0b10111111;
        }
    }
    // 4. RESTORE EVERYTHING
    flagsMirror = oldFlags;
    banksMirror = oldBanks;
    *dma_flags = flagsMirror;
    *bank_reg = banksMirror;
}

#define LIVESRIGHT (BRICK_RIGHT - 4)
#define LIVESYPOS (CEILING - 2)
#define LIVESWIDTH (BALLSIZE+2)
// void draw_lives_prog1()
// {
//     char i;
//     for (i=0;i<)
// }

#pragma code-name (pop);
void EraseRect(char x, char y, char w, char h) {
    queue_draw_sprite(x, y, w, h,x,y, SPRITEBANKBAKEDBACKGROUND); 
}

// void GameWrapper()
// {
//     bool buttonAPress;
//     bool buttonBPress;
//     // if (demoMode || gamestate==STATE_ATTRACT) {
//     //     change_rom_bank(BANK_PROG2);
//     //     update_paddle_offset_prog2();
//     // }
//     buttonAPress = (player1_buttons & INPUT_MASK_A) && (~player1_old_buttons & INPUT_MASK_A);
//     buttonBPress = (player1_buttons & INPUT_MASK_B) && (~player1_old_buttons & INPUT_MASK_B);

//     if (gameState == STATE_CONTROLLER_SELECT)
//     {
//         change_rom_bank(BANK_PROG1);
//         BreakoutBGRefresh_prog1();

//         change_rom_bank(BANK_PROG2);
//         SelectControllerScreen_prog2(buttonAPress,buttonBPress);
//     }
//     else
//     {
//         //BreakoutGameWrapper();
//     }
// }
void GameWrapperStateMachine()
{
    bool buttonCPress;
    bool buttonAPress;
    bool buttonBPress;
    bool buttonStartPress;
    
    eraseBlinkMessage(&messageEraseCount);
    change_rom_bank(BANK_PROG1);
    erase_paddle_prog1();
    //sample inputs first
    buttonAPress = (player1_buttons & INPUT_MASK_A) && (~player1_old_buttons & INPUT_MASK_A);
    buttonBPress = (player1_buttons & INPUT_MASK_B) && (~player1_old_buttons & INPUT_MASK_B);
    buttonCPress = (player1_buttons & INPUT_MASK_C) && (~player1_old_buttons & INPUT_MASK_C);
    buttonStartPress = player1_buttons & INPUT_MASK_START && ~player1_old_buttons & INPUT_MASK_START;
    // if (buttonStartPress)//toggle demo mode automated/player modes
    // {
    //     demoMode = !demoMode;
    //     if (!demoMode) messageEraseCount+=2;
    // }
    // change_rom_bank(BANK_PROG1);
    button_byte = buttons_to_byte_xyzm_prog1(player1_buttons);//gets paddle input
    BreakoutBGRefresh_prog1();//always need the black background and borders

    switch (gameState) {
        case STATE_CONTROLLER_SELECT:
        change_rom_bank(BANK_PROG2);
        SelectControllerScreen_prog2(buttonAPress,buttonBPress);
        break;

        case STATE_ATTRACT:
            change_rom_bank(BANK_PROG2);
            update_paddle_offset_prog2();
        case STATE_BREAKOUT_PLAY:
            change_rom_bank(BANK_PROG1);
            DrawBricksWrapper_prog1();//blits
            if (subState != SUBSTATE_NEW_LEVEL) process_all_balls_prog1();//not needed for new level but no harm
            erase_balls_prog1();//blits
            if (gameState == STATE_ATTRACT) printBlinkMessage_prog1(0);//message before balls
            render_balls_prog1();//blits
            update_player_paddle_prog1();//cpu
            render_paddle_prog1();//blits
            switch (subState){
                case SUBSTATE_NEW_LEVEL:
                //DrawBricksWrapper_prog1();//blits
                //erase_balls_prog1();//blits
                //if (gameState == STATE_ATTRACT) printBlinkMessage_prog1(0);//message before balls
                //render_balls_prog1();//blits
                //update_player_paddle_prog1();//cpu
                //render_paddle_prog1();//blits
                if (resetTimer>0){
                    resetTimer--;
                }
                else{
                    GenNextLevel_prog1(cycleLevel++ % NUM_BRICK_LAYOUTS);
                    resetTimer=100;
                    NewLevel_prog1();
                    subState=SUBSTATE_PLAY;
                }
                break;

                case SUBSTATE_PLAY://ball(s) are in play                
                //DrawBricksWrapper_prog1();//handles full-screen refresh and optimized rendering
                //process_all_balls_prog1();
                //erase_balls_prog1();
                //if (gameState == STATE_ATTRACT) printBlinkMessage_prog1(0);//message before balls
                //render_balls_prog1();
                //update_player_paddle_prog1();
                //render_paddle_prog1();

                //if (subState==SUBSTATE_PLAY)
                //{//ball(s) in play block
                    #if TESTFEATURES
                    if (buttonCPress){
                        subState = SUBSTATE_NEW_LEVEL;
                        messageEraseCount=2;
                    }
                    #endif
                    if (bricks_remaining<=0) {
                        resetTimer=100;
                        subState = SUBSTATE_NEW_LEVEL;
                    } 
                    else if (get_active_ball_count_prog1()<=0) //no balls in play
                    {
                        // if (lives_left == 0) 
                        // {
                        //     // This is where you decide: New Life or Game Over?
                        //     gameState = STATE_BREAKOUT_GAMEOVER;
                        //     resetTimer = 500;
                        // }
                        // else
                        {
                            //if (gameState==STATE_BREAKOUT_PLAY) 
                            //{
                                lives_left--;
                            //}//infinite lives in attract mode
                            subState = SUBSTATE_NEW_BALL; //player serves ball or it auto-spawns
                            resetTimer=100;
                        }
                        if (lives_left == 0) 
                        {
                            // This is where you decide: New Life or Game Over?
                            gameState = STATE_BREAKOUT_GAMEOVER;
                            resetTimer = 500;
                        }
                    }
                    if ((gameState == STATE_ATTRACT) && (buttonStartPress || buttonAPress)){
                        init_game_prog1();
                        gameState = STATE_BREAKOUT_PLAY;
                        subState = SUBSTATE_NEW_LEVEL;
                        messageEraseCount=2;
                    }
                //}
                break;
                case SUBSTATE_NEW_BALL://no ball in play, delay, spawn new ball

                //else if (subState==SUBSTATE_NEW_BALL)
                //{//ball spawn block
                    if (resetTimer>0){//timer for ball spawn
                        resetTimer--;
                    }
                    else{
                        resetTimer=100;
                        spawn_new_rnd_ball_prog1();
                        subState=SUBSTATE_PLAY;
                    }
                //}
                break;
            }
        break;

        case STATE_BREAKOUT_GAMEOVER:
        resetTimer--;
        if (resetTimer<=0){
            init_game_prog1();
            gameState = STATE_ATTRACT;
            messageEraseCount=2;
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
void queue_erase(char x,char y, char w, char h,char frames){
    if (erase_queue_count < ERASE_QUEUE_MAX) {
            erase_queue[erase_queue_count].frames_left = frames; // Two buffers to clean
            erase_queue[erase_queue_count].x = x;
            erase_queue[erase_queue_count].y = y;
            erase_queue[erase_queue_count].w = w;
            erase_queue[erase_queue_count].h = h;
            erase_queue_count++;
    }
}
#define SCOREPOSX 5
#define HISCOREPOSX 50//(64 - ((7 * 4)/2)) //46

#define SCOREWIDTH 36
void main () {
    init_music();
    load_spritesheet(ASSET__gfx__paddle_bmp,SPRITEBANKPADDLE);
    load_spritesheet(ASSET__gfx__PressStart_bmp,SPRITEBANKPRESSSTART);

    scoring_init();
    change_rom_bank(BANK_PROG1);
    BakeBreakoutBackground_prog1(SPRITEBANKBAKEDBACKGROUND);//bank 1

    bake_gradient_mask_prog1();//only needs to be done once
    init_game_prog1();
    init_ball_system_prog1();
    BreakoutBricksSetupWrapper_prog1(SPRITEBANKBRICKS); //redundant but keeps vram properly setup
    gameState = STATE_CONTROLLER_SELECT;

    while (1) 
    {                                     //  Run forever

        //GameWrapper();
        GameWrapperStateMachine();
        queue_clear_border(2);//color

        //queue_draw_box(4,10,33,9,0);//clear scores with bg sprite

        if (scoreRefreshFrames)EraseRect(SCOREPOSX-1,10,SCOREWIDTH,9);//clear scores from previous frames
        if (hiscoreRefreshFrames)EraseRect(HISCOREPOSX-1,10,SCOREWIDTH,9);//clear scores from previous frames
        
        await_draw_queue();

        if (scoreRefreshFrames)print_scores(score,SCOREPOSX);
        scoreRefreshFrames--;

        if (hiscoreRefreshFrames)print_scores(hiscore,HISCOREPOSX);
        hiscoreRefreshFrames--;

        await_vsync(1);
        flip_pages();
        tick_music();
        update_inputs();
    }
    
}