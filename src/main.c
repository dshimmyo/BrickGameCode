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
unsigned char currentBrickWidth = 12;//8;//4;//2;
unsigned char currentNumBricksH = 10;//15;//30;//60;



#define CEILING 21

#define BRICK_TOP    35//39//25 + 18
#define BRICK_BOTTOM 58//58//44 + 18
#define BRICK_LEFT   4//6//4//4 pixels border but maybe only 3 visible
#define BRICK_RIGHT  123//123//4 pixels border
#define BRICK_ANIMATION true

#define MAX_NOISE 64

#define STATE_CONTROLLER_SELECT 10
#define STATE_BREAKOUT_MENU 20
#define STATE_BREAKOUT_SERVE 25
#define STATE_BREAKOUT_PLAY 30
#define STATE_BREAKOUT_GAMEOVER 40
#define STATE_BREAKOUT_LEADERBOARD 50

#define CONTROLLER_STYLE_PADDLE0 0
#define CONTROLLER_STYLE_GPAD1 1
#define MAX_BALLS 8 //planning for insane mode


char gamestate = 0;
char controller_style = 0;
char numLives = 3;
char breakoutRefresh=0;//set to true to refresh the whole screen
char breakoutBricksRefresh=0;//set to true to refresh the whole screen
unsigned char bricks_remaining = 0;

//prototypes
void check_brick_collision_prog1(char *,char *, int *, int *, int *);
void randomizeBox(char *_box_x, char *_box_y, int *_dx, int *_dy, int *_ballSpeed);
int speedMult_prog1(int source, int speed);
void BrickHeal_prog1(unsigned char, unsigned char, unsigned char);
void process_anim_queue_prog1();
void BreakoutBricksSetupWrapper_prog1(char);


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
    char row;
    char col;
} EraseTask;

typedef struct {
    char numColumns;//allocate max number of bricks
    char brickWidth;
} BrickLayout;

typedef struct { 
    char x, y; //signed just in case, to avoid overflow when negative
    char x_old, y_old;         // Last frame's position
    char x_old_old, y_old_old; // Position from 2 frames ago (for double-buffering)
    int dx, dy; // Normalized direction (-1, 0, 1) 
    int dxRem, dyRem; // sub-pixel remainders
    int speed; // 1 = 5/12, 2 = 10/12, etc. 
    bool active; 
} Ball;

Ball balls[MAX_BALLS];//pre-allocate enough space for MAX_BALLS

#define NUM_ANGLES 22
#pragma rodata-name (push, "PROG1")
const angle reflectAnglesNew_prog1[NUM_ANGLES]=
{{-251<<1,-30<<1},{-247<<1,-61<<1},{-235<<1,-86<<1},{-224<<1,-112<<1},{-202<<1,-146<<1},{-181<<1,-181<<1},{-146<<1,-202<<1},{-112<<1,-224<<1},{-86<<1,-235<<1},{-61<<1,-247<<1},{-30<<1,-251<<1},
{30<<1,-251<<1},{61<<1,-247<<1},{86<<1,-235<<1},{112<<1,-224<<1},{146<<1,-202<<1},{181<<1,-181<<1},{202<<1,-146<<1},{224<<1,-112<<1},{235<<1,-86<<1},{247<<1,-61<<1},{251<<1,-30<<1}};
#pragma rodata-name (pop)

unsigned char brickRowPoints[6]={4,3,2,2,1,1};//number of points gained by hitting a brick in each row, top to bottom

EntityRow brickRows[NUM_BRICK_ROWS];//init in NewLevel()

EraseTask erase_queue[8];
unsigned char erase_queue_count = 0;

#define ERASE_QUEUE_MAX 16
#define NUM_ANIM_FRAMES 8
#define ANIM_QUEUE_MAX 4
EraseTask anim_queue[ANIM_QUEUE_MAX];//[NUM_ANIM_FRAMES*2*MAX_BALLS];//10 frames of animation over maybe double frames for up to two balls animframes * 2X * maxballs
unsigned char  anim_queue_count = 0;

unsigned char animatedBricks[MAX_BRICK_COLS*NUM_BRICK_ROWS]; //this needs to be re-engineered

#define NUM_BRICK_LAYOUTS 14
BrickLayout brickLayouts[NUM_BRICK_LAYOUTS]=
{
    //{cols,width}
    {1,120},
    {2,60},
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
    {60,2},
    {120,1}
};

bool demoMode = true;
bool debugMode = false;

//boxes, make more modular
char box_x = 64, box_y = 64;
char box_x_old = 64, box_y_old = 64;
char box_x_old_old = 64, box_y_old_old = 64;

char boxA_x = 64, boxA_y = 64;
char boxA_x_old = 64, boxA_y_old = 64;
char boxA_x_old_old = 64, boxA_y_old_old = 64;

int ballSpeed = 1;//actual speed is in .5 increments
int ballSpeedA = 1;

//ball movement variables
//256 is 1 pixel per frame
int dx = 256, dy = 256;//range -256 to 256
int dxA = 256, dyA = 256;
int dxRem=0;//remainder
int dyRem=0;

char paddleX = 64;//global paddle xpos
char paddleX_old=64;
char paddleX_old_old=64;
unsigned char button_byte=0;//8-bit byte representing button states, for debugging and potential use in game mechanics
unsigned int score = 0;//global score variable
int resetTimer = 100;//delay between brick reset and box reset
char rndPaddleOffset = 0;
unsigned char rndPaddleOffsetTimer = 0;

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


#pragma code-name (push, "PROG2")
void spawn_ball_prog2(char x, char y, int dx, int dy, int speed) {
    unsigned char i;
    for (i = 0; i < MAX_BALLS; i++) {
        if (!balls[i].active) {
            balls[i].x = x;
            balls[i].y = y;
            balls[i].dx = dx;
            balls[i].dy = dy;
            balls[i].dxRem = 0;
            balls[i].dyRem = 0;
            balls[i].speed = speed;
            balls[i].active = true;
            return; // Exit once the ball is placed
        }
    }
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
        if (_dx > reflectAnglesNew_prog1[index].dx) newDiff = dx - reflectAnglesNew_prog1[index].dx;
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
    //if (sourceXLow == paddleX+PADDLEWIDTH-1) paddleHitRegion = 7;
    if (sourceXLow > paddleX) paddleHitRegion = (sourceXLow - paddleX)/(PADDLEWIDTH / 8);
    else paddleHitRegion = 0;
    //paddleHitRegion = (paddleHitRegion < 0) ? 0 : paddleHitRegion;
    //boxes draw left-right top-down 0->127, 7->120
    //source is ball, target is paddle
    //targetLow should be same as PADDLEY, redundant
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
#pragma code-name (pop)


#pragma code-name (push, "PROG1")
// int approx_div_3_prog1(int num) {
//     return (num >> 5) * 11; // This is an approximation using a fixed-point representation
// }
int approx_div_3_prog1(int num) {
    return (num >> 6) * 21; // This is an approximation using a fixed-point representation
}
int approx_five_twelfths(int num){
    return (num >> 5) * 13;
}
int speedMult_prog1(int source,int mult){//multiplier can be fine-tuned here and hidden from code
    //return (source>>1) * mult; //half increments
    //return (source/3) * mult; //third increments, more work for cpu
    return approx_five_twelfths(source) * mult;
}
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
            randomizeBox(&box_x, &box_y, &dx, &dy, &ballSpeed);
            soundTest();
        }

        if(box_x <= BRICK_LEFT) {
            dx = (dx<0) ? -dx : dx;
            soundWall();
        } else if(box_x >= BRICK_RIGHT-BALLSIZE+1 /*119*/) {
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
}
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

    // Check if we've accumulated enough for a whole pixel move (>= 256)
    if (((unsigned int)dxTot >= 255 || (unsigned int)dyTot >= 255)) 
    {
        b->x += dxTot >> 8;
        b->y += dyTot >> 8;

        // --- CEILING / OUT OF BOUNDS ---
        if(b->y <= CEILING + 1) {
            b->dy = (b->dy < 0) ? -b->dy : b->dy;
            soundCeiling();
        } 
        else if(b->y >= 120 - BALLSIZE) {
            // For now, we randomize/reset. Later, we can set b->active = false;
            //randomizeBox(&b->x, &b->y, &b->dx, &b->dy, &b->speed);
            b->active = false;//despawn
            soundTest();
        }

        // --- WALLS ---
        if(b->x <= BRICK_LEFT) 
        {
            b->dx = (b->dx < 0) ? -b->dx : b->dx;
            soundWall();
        } 
        else if(b->x >= BRICK_RIGHT - BALLSIZE + 1) 
        {
            b->dx = (b->dx > 0) ? -b->dx : b->dx;
            soundWall();
        } 
        
        // --- PADDLE ---
        if (detectPaddleCollision_prog1(b->x, b->x + BALLSIZE, b->y, b->y + BALLSIZE, &b->dx, &b->dy, &b->speed)) 
        {
        // The pointers inside the function will now update the 
        // specific ball's dx, dy, and speed in the array.
        b->dyRem = 0; 
        }

        // --- BRICKS ---
        // Pass the ball's specific variables to your collision detector
        check_brick_collision_prog1(&b->x, &b->y, &b->dx, &b->dy, &b->speed);
    }

    // Update the remainder for the next frame
    b->dxRem = dxTot & 255;
    b->dyRem = dyTot & 255;
}


#pragma code-name (pop)


int dxARem=0;//remainder
int dyARem=0;

#pragma code-name (push, "PROG1")
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
        } else if(boxA_y >= 120-BALLSIZE/*112*/) {
            randomizeBox(&boxA_x, &boxA_y, &dxA, &dyA, &ballSpeedA);
            soundTest();
        }

        if (boxA_x <= BRICK_LEFT) {
            dxA = (dxA<0) ? -dxA : dxA;
            soundWall();
        } else if(boxA_x >= BRICK_RIGHT-BALLSIZE+1/*119*/) {
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
}
#pragma code-name (pop)

#pragma code-name (push, "PROG2")
void update_paddle_offset_prog2(){
    if (rndPaddleOffsetTimer++ > 128)
    {
        rndPaddleOffset = rnd_range(-(PADDLEWIDTH>>1),(PADDLEWIDTH>>1));
        rndPaddleOffsetTimer=0;
    }
}
// Convert individual button states to a 8-bit byte
unsigned char buttons_to_byte_xyzm_prog2(int player1_buttons) {
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
#pragma code-name (pop)


// int setRange(char input, char inMin, char inMax, char outMin, char outMax)
// {
//     int output = 0;
//     if (input<inMin)return outMin;
//     else if (input>inMax)return outMax;

//     output = ((outMax - outMin)<<8/(inMax-inMin)*(input-inMin))>>8 + outMin;
//     return output;
    
// }
// char ClampLeft(int x){
//     if (x<1){return 1;}
//     return x;
// }
// int SlowLerp(int source, int target)
// {
//     //return target/3 + (source<<1)/3;//1/3 effort
//     //return (target>>2) + ((source*3)>>2);//1/4 effort
//     //return (target/5) + ((source<<2)/5);//1/5 effort
//     //return (target/6) + ((source*5)/6);//1/6 effort
//     //return (target/7) + ((source*6)/7);//1/7 effort
//     return (target>>3) + ((source*7)>>3);//1/8 effort
// }

#pragma code-name (push, "PROG1")
int ClampPaddleX_prog1(int paddlex){
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
int paddleXFromDPad_prog1()
{
    int result = paddleX;
    if ((player1_buttons & INPUT_MASK_RIGHT)) result += 3;
    else if ((player1_buttons & INPUT_MASK_LEFT)) result -= 3;
    return ClampPaddleX_prog1(result);
}
int ConstVelocity_prog1(char source, char target, char vel){
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


void ToggleDemoMode()
{
    if (player1_buttons & INPUT_MASK_START && ~player1_old_buttons & INPUT_MASK_START)
    {
        demoMode = !demoMode;
    }
}

void randomizeBox(char *_box_x, char *_box_y, int *_dx, int *_dy, int *_ballSpeed){
    *_box_x = rnd_range(5,114);
    *_box_y = rnd_range(64,68);
    *_dx = (rnd_range(0,10) > 5) ? 255 : -255;
    *_dy = 255;//always down
    *_ballSpeed = 1;//default slow
}

unsigned char cycleLevel=0;
void GenNextLevel(char procLevel)
{
    BrickLayout bl;
    //bl = brickLayouts[cycleLevel++ % NUM_BRICK_LAYOUTS];
    bl = brickLayouts[procLevel % NUM_BRICK_LAYOUTS];
    currentBrickWidth = bl.brickWidth;
    currentNumBricksH = bl.numColumns;
    change_rom_bank(BANK_PROG1);
    BreakoutBricksSetupWrapper_prog1(3);
}
void NewLevel()
{
    unsigned char x;
    unsigned char y;
    int count = 0;
    
    for (y = 0; y<NUM_BRICK_ROWS; y++){
        for (x=0; x<currentNumBricksH; x++){
            brickRows[y].visible[x] = 1;
            brickRows[y].erase_frames[x] = 0;
            //animatedBricks[count++]=0;//set all bricks to unanimated
        }
    }
    erase_queue_count=0;
    anim_queue_count=0;
    ballSpeed = 1;//slow start
    ballSpeedA = 1;//slow start
    bricks_remaining = currentNumBricksH * NUM_BRICK_ROWS;
    randomizeBox(&box_x, &box_y, &dx, &dy, &ballSpeed);
    randomizeBox(&boxA_x, &boxA_y, &dxA, &dyA, &ballSpeedA);
    breakoutRefresh=0;
    breakoutBricksRefresh=0;
}
void NewGame()
{
    numLives = 3;
    score = 0;
}
void init_game()//used to refresh the screen when all the bricks are gone
{
    numLives = 3;
    //NewLevel();
    resetTimer = 100;
}

unsigned char ClampX(unsigned char num)
{
    unsigned char result = num;
    result = (result > 127) ? 127 : result;
    return result;
}


#pragma code-name (push, "PROG1")
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
                queue_draw_sprite(xDrawStart,posy,brickDrawWidth,BRICK_HEIGHT,xDrawStart,(row*BRICK_HEIGHT)/*+60*/,3);

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
                        queue_draw_sprite(BRICK_LEFT + col * currentBrickWidth,BRICK_TOP + BRICK_HEIGHT * row,currentBrickWidth,BRICK_HEIGHT,BRICK_LEFT + col * currentBrickWidth,row*BRICK_HEIGHT+offset,3);//paint box
                    }
                }
            } 
        }
    }
}
#pragma code-name (pop)

#pragma code-name (push, "PROG1")
void DrawBricksErase_prog1(){ //erase bricks in the queue
    unsigned char row;
    unsigned char col;
    //int idx;
    unsigned char posy;
    int i;

    for (i = 0; i < erase_queue_count; i++) 
    {
        //idx = erase_queue[i].brick_index;
        row = erase_queue[i].row;//idx / currentNumBricksH;
        col = erase_queue[i].col;//idx % currentNumBricksH;
        posy = BRICK_TOP + BRICK_HEIGHT * row;//brickRow[y].posy;//brickRowYPos[y];

        // Draw the "hole" in the current back-buffer
        queue_draw_box(BRICK_LEFT + col * currentBrickWidth,posy,currentBrickWidth,BRICK_HEIGHT,0);//paint box
        erase_queue[i].frames_left--;

        // If it's been erased from both buffers, remove it from the queue
        if (erase_queue[i].frames_left == 0) {
            erase_queue[i] = erase_queue[erase_queue_count - 1]; // Swap with last
            erase_queue_count--;
            i--; // Re-check this slot since we swapped
        }
    }
    /*
    for (row=0;row<NUMBRICKSV;row++)  
    {
        unsigned char posy = BRICK_TOP + BRICKHEIGHT * row;//brickRow[y].posy;//brickRowYPos[y];
        unsigned char col;
        unsigned char animFrame;
        unsigned char eraseFrame;
        for (col=0;col<NUMBRICKSH;col++)
        {
            if (!brickRows[row].visible[col]){
                animFrame = animatedBricks[row*NUMBRICKSH + col];
                //eraseFrame = brickRows[row].erase_frames[col];
                // if (eraseFrame){
                //     queue_draw_sprite(BRICK_LEFT + col * BRICKWIDTH,posy,BRICKWIDTH,BRICKHEIGHT,BRICK_LEFT + col * BRICKWIDTH,posy,2);//paint from bg bank
                //     brickRows[row].erase_frames[col]--;
                // }
                //else 
                if (animFrame)
                {
                    unsigned char xDrawStart = BRICK_LEFT + col * BRICKWIDTH;
                    unsigned char gx = ((20-animFrame)>>1)*BRICKWIDTH;//calculate the growing x offset based on the animation frame (0-6)
                    queue_draw_sprite(BRICK_LEFT + col * BRICKWIDTH,posy,BRICKWIDTH,BRICKHEIGHT,BRICK_LEFT + col * BRICKWIDTH,posy,2);//paint from bg bank
                    queue_draw_sprite(BRICK_LEFT + col * BRICKWIDTH,posy,BRICKWIDTH,BRICKHEIGHT,gx,(row*BRICKHEIGHT)+80,3);
                    animatedBricks[row*NUMBRICKSH + col]--;
                }
            }
        }
    }
    */

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
            erase_queue[erase_queue_count].row = row;
            erase_queue[erase_queue_count].col = col;
            erase_queue_count++;
        }
        if (anim_queue_count < ANIM_QUEUE_MAX)
        {
            //anim_queue[anim_queue_count].brick_index = row*currentNumBricksH + col;
            anim_queue[anim_queue_count].frames_left = NUM_ANIM_FRAMES*2+2;//double buffers?
            anim_queue[anim_queue_count].row = row;
            anim_queue[anim_queue_count].col = col;
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
        score+= brickRowPoints[row];//brickRows[row].points;//(7-row)>>1;
        if (row==0) *_ballSpeed = 3;//not double speed but 3/2 speed
    }
    return;
}
#pragma code-name (pop)

#pragma code-name (push, "PROG1")
void BreakoutBGRefresh_prog1()
{
    if (breakoutRefresh<2){
        //queue_clear_border(2);

        queue_clear_screen(0);
        queue_draw_box(1,7,128,3,0b01111000);//top border
        queue_draw_box(1,(CEILING-2),126,2,0b01111000);//ceiling bar
        queue_draw_box(0,7,BRICK_LEFT,127,0b01111000);//left bar
        queue_draw_box(BRICK_RIGHT+1,7,3,113,0b01111000);//rt bar

        //old sprite bg
        // queue_draw_sprite(1,7,126,113,1,1,2);//bg in bank 2
        // queue_draw_sprite(1,(CEILING-2),126,2,1,1,2);//ceiling bar
        breakoutRefresh++;//runs twice to cover two buffers
    }
    else//clean up edges
    {
        //queue_draw_sprite(1,7,126,113,1,1,2);//bg in bank 2 //full bg

        //queue_draw_sprite(1,(CEILING-2),126,2,1,1,2);//ceiling bar
        queue_draw_box(1,(CEILING-2),126,2,0b01111000);//ceiling bar - fixes ball ghosts

        //queue_draw_sprite(1,7,3,127,1,7,2);//left bar
        queue_draw_box(0,7,BRICK_LEFT,127,0b01111000);//left bar - fix ball ghosts

        //queue_draw_sprite(124,7,3,113,124,1,2);//rt bar
        queue_draw_box(BRICK_RIGHT+1,7,3,113,0b01111000);//rt bar - fix ball ghosts

    }
}

#pragma code-name (pop)

void printPressStart(char x, char y, bool visible){
    if (visible){
        queue_draw_sprite(x,y,52,6,0,14,4);//press start
    }else {
        queue_draw_box(x,y,52,6,0);//press start heal
    }
}
void printGameOver(char x, char y, bool visible){
    if (visible){
        queue_draw_sprite(x,y,52,6,0,32,4);//press start
    }else {
        queue_draw_box(x,y,52,6,0);//press start heal
    }
}
int blinkTimer=0;
char pressStartVisible=0;
void BreakoutGameWrapper(){
    change_rom_bank(BANK_PROG1);
    BreakoutBGRefresh_prog1();

    //cover up previous paddle position
    queue_draw_box(paddleX_old_old,PADDLEY,PADDLEWIDTH,PADDLEHEIGHT,0);//heal paddle

    //erase where ball has previously been
    queue_draw_box(box_x_old_old,box_y_old_old,BALLSIZE,BALLSIZE,0);//heal box
    queue_draw_box(boxA_x_old_old,boxA_y_old_old,BALLSIZE,BALLSIZE,0);//heal boxA
    
    change_rom_bank(BANK_PROG1);
    BrickHeal_prog1(box_x_old_old,box_y_old_old,0);
    BrickHeal_prog1(boxA_x_old_old,boxA_y_old_old,0);
    if (demoMode)
    {
        if (blinkTimer++>0){
            //queue_draw_sprite(38,64,52,6,0,14,4);//press start
            printPressStart(38,64,true);
            pressStartVisible=2;
            blinkTimer = (blinkTimer>45) ? blinkTimer=-80 : blinkTimer;
        }
        else
        {
            if (pressStartVisible) {
                //queue_draw_box(38,64,52,6,0);//press start heal
                printPressStart(38,64,false);
                pressStartVisible--;
            }
        }
    }
    else //make sure to erase press start
    {
        if (pressStartVisible) {
            queue_draw_box(38,64,52,6,0);//press start heal
            pressStartVisible--;
        }
    }
    if (!bricks_remaining || gamestate==STATE_BREAKOUT_MENU) {
        if (resetTimer--==100){
            gamestate=-1;//not sure what to do here
        }
        if (!resetTimer) {
            GenNextLevel(cycleLevel++ % NUM_BRICK_LAYOUTS);

            resetTimer=100;
            NewLevel();
            gamestate=STATE_BREAKOUT_PLAY;
            //init_game();
        }
    }

    change_rom_bank(BANK_PROG2);
    button_byte = buttons_to_byte_xyzm_prog2(player1_buttons);//gets paddle input

    if (player1_buttons & INPUT_MASK_A && ~player1_old_buttons & INPUT_MASK_A) 
    {
        debugMode = !debugMode;
    }
    if (debugMode){
        change_rom_bank(BANK_PROG2);
        inputButtonsDraw_prog2();//debug display
        // change_rom_bank(BANK_PROG2);
        inputBinaryDraw_prog2();//debug line
    }
// change_rom_bank(BANK_PROG1);
    change_rom_bank(BANK_PROG1);

    DrawBricksWrapper_prog1();

    if (gamestate==STATE_BREAKOUT_PLAY || gamestate == STATE_BREAKOUT_MENU){
    // change_rom_bank(BANK_PROG1);
    boxMotion_prog1();
    // change_rom_bank(BANK_PROG1);
    boxAMotion_prog1();
    }

    ToggleDemoMode();
    change_rom_bank(BANK_PROG1);
    if (demoMode){
        paddleX = paddleXFromClosestBox_prog1();
    } else {
        if (controller_style == CONTROLLER_STYLE_PADDLE0){
            //change_rom_bank(BANK_PROG1);
            paddleX = paddleXFromPot8opt_prog1(button_byte);
        }
        else if (controller_style == CONTROLLER_STYLE_GPAD1)
        {
            //change_rom_bank(BANK_PROG1);
            paddleX = paddleXFromDPad_prog1();
        }
    }

    if (gamestate==STATE_BREAKOUT_PLAY){
        queue_draw_sprite(box_x,box_y,BALLSIZE,BALLSIZE,4,0,1);//ball
        queue_draw_sprite(boxA_x,boxA_y,BALLSIZE,BALLSIZE,4,0,1);
    }
    queue_draw_sprite(paddleX,PADDLEY,PADDLEWIDTH,PADDLEHEIGHT,0,0,1);
   // print_scores(score);
    paddleX_old_old = paddleX_old;
    paddleX_old = paddleX;
    box_x_old_old = box_x_old;
    box_x_old = box_x;
    box_y_old_old = box_y_old;
    box_y_old = box_y;
    boxA_x_old_old = boxA_x_old;
    boxA_x_old = boxA_x;
    boxA_y_old_old = boxA_y_old;
    boxA_y_old = boxA_y;
}

#pragma code-name (push, "PROG2")
void SelectControllerScreen_prog2()
{
    char top = 60;
    
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
    if ((player1_buttons & INPUT_MASK_B) && !(player1_old_buttons & INPUT_MASK_B)) 
    {
        controller_style = CONTROLLER_STYLE_GPAD1;
        gamestate = STATE_BREAKOUT_MENU;
        //NewLevel();
    }
    else if ((player1_buttons & INPUT_MASK_A) && !(player1_old_buttons & INPUT_MASK_A)) 
    {
        controller_style = CONTROLLER_STYLE_PADDLE0;
        gamestate = STATE_BREAKOUT_MENU;
        //NewLevel();
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
    unsigned char pixel;

    // 1. SET THE HARDWARE MODE
    // Clear Bit 0 (DMA_ENABLE) and Clear Bit 1 (DMA_CPU_TO_VRAM)
    flagsMirror &= ~(DMA_ENABLE | DMA_CPU_TO_VRAM); 
    *dma_flags = flagsMirror;

    // 2. BANK IN THE SPRITES
    banksMirror = bankflip | GRAM_PAGE(3);
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
/*
void animate_dissolve_prog1(unsigned char col, unsigned char row, unsigned char frame) {
    char oldFlags = flagsMirror; // Save the original screen/DMA settings
    char oldBanks = banksMirror; 
    
    int brick_base;
    unsigned char x, y;
    unsigned char threshold = 255 - (frame * 255/NUM_ANIM_FRAMES>>1);
    int row_start = (row << 2) << 7 ;//* 128 ; //0x000 + (row * 4 * 128);//was 0x181

    // 1. SET THE HARDWARE MODE
    // Clear Bit 0 (DMA_ENABLE) and Clear Bit 1 (DMA_CPU_TO_VRAM)
    flagsMirror &= ~(DMA_ENABLE | DMA_CPU_TO_VRAM); 
    *dma_flags = flagsMirror;

    // 2. BANK IN THE SPRITES
    banksMirror = bankflip | GRAM_PAGE(3);
    *bank_reg = banksMirror;

    // Calculate the top-left corner of THIS specific brick in VRAM
    // row * 4 (height) * 128 (stride) + col * currentBrickWidth
    brick_base = row_start + (col * currentBrickWidth);// + BRICK_LEFT;
    for(y = 0; y < 4; y++) {
        for(x = 0; x < currentBrickWidth; x++) {
            unsigned int target_pos = brick_base + (y << 7) + x; // y << 7 is y * 128

            if (frame<=2)
            {
                vram[target_pos] = 0; // Erase the pixel
            }
            else
            {
                // target_pos is the base + current row offset + current column offset

                // Check against your 'Master Gradient' mask
                if (dissolve_mask[(y * currentBrickWidth) + x] < threshold) {
                    vram[target_pos] = 0; // Erase the pixel
                }
            }
        }
    }
    // 4. RESTORE EVERYTHING
    flagsMirror = oldFlags;
    banksMirror = oldBanks;
    *dma_flags = flagsMirror;
    *bank_reg = banksMirror;
}*/
void process_anim_queue_prog1() {
    int i;
    unsigned int animOffsetY = 24;
    unsigned int animOffsetX;
    char animFrame;
    unsigned int animFrameMaxPixel;
    for (i = 0; i < anim_queue_count; i++) {
    //for ( i = anim_queue_count - 1; i >= 0; i--) {
        EraseTask *task = &anim_queue[i];
        
        // 1. Only update the SOURCE pixels in Bank 3 once per logic frame
        // This 'eats' the brick further in the master spritesheet
        //animate_dissolve_prog1(task->col, task->row, task->frames_left);

        // 3. Draw the resulting sprite to the screen
        queue_draw_box(BRICK_LEFT + task->col * currentBrickWidth,BRICK_TOP + task->row * BRICK_HEIGHT,currentBrickWidth,BRICK_HEIGHT,0);
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
            queue_draw_sprite(BRICK_LEFT + task->col * currentBrickWidth,BRICK_TOP + task->row * BRICK_HEIGHT,currentBrickWidth,BRICK_HEIGHT, animOffsetX, (task->row * BRICK_HEIGHT) + animOffsetY,3);//brick sprite has no y offset
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
    banksMirror = bankflip | GRAM_PAGE(3);
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
    for (i = 0; i < MAX_BALLS; i++) {
        if (balls[i].active) {
            update_ball_logic_prog1(&balls[i]);
        }
    }
}
void init_ball_system_prog1() {
    unsigned char i;
    for (i = 0; i < MAX_BALLS; i++) {
        balls[i].active = false;
        balls[i].x = 0;
        balls[i].y = 0;
        balls[i].x_old = 0;
        balls[i].y_old = 0;
        balls[i].x_old_old = 0;
        balls[i].y_old_old = 0;
    }
}
#pragma code-name (pop);


void GameWrapper()
{
    if (demoMode) {
        change_rom_bank(BANK_PROG2);
        update_paddle_offset_prog2();
    }

    if (gamestate == STATE_CONTROLLER_SELECT)
    {
        change_rom_bank(BANK_PROG1);
        BreakoutBGRefresh_prog1();
        await_draw_queue();

        change_rom_bank(BANK_PROG2);
        SelectControllerScreen_prog2();
    }
    else
    {
        change_rom_bank(BANK_PROG1);
        BreakoutGameWrapper();
    }
}

void main () {
    init_music();
    load_spritesheet(ASSET__gfx__paddle_bmp,1);
    load_spritesheet(ASSET__gfx__BreakoutBrickBG_bmp,2);
    load_spritesheet(ASSET__gfx__PressStart_bmp,4);
    change_rom_bank(BANK_PROG1);
    bake_gradient_mask_prog1();//only needs to be done once
    init_game();
    scoring_init();
    change_rom_bank(BANK_PROG1);
    init_ball_system_prog1();
    BreakoutBricksSetupWrapper_prog1(3); //redundant but keeps vram properly setup
    gamestate = STATE_CONTROLLER_SELECT;

    while (1) 
    {                                     //  Run forever
        GameWrapper();
        queue_clear_border(2);
        queue_draw_box(4,10,33,9,0);//clear scores with bg sprite
        await_draw_queue();
        print_scores(score);
        await_vsync(1);
        flip_pages();
        tick_music();
        update_inputs();
    }
    
}