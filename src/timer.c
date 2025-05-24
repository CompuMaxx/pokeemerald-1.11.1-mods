#include "global.h"
#include "battle_anim.h"
#include "decompress.h"
#include "event_data.h"
#include "gpu_regs.h"
#include "malloc.h"
#include "main.h"
#include "palette.h"
#include "digit_obj_util.h"
#include "sound.h"
#include "sprite.h"
#include "start_menu.h"
#include "task.h"
#include "constants/songs.h"

#define MAX_TIME (6000) // Tiempo máximo en seg. puede llegar hasta 100:00:00 (100 minutos o 1:40 horas)
#define TAG_TIMER_DIGITS  4

#define tId                 data[0]
#define tState              data[1]
#define tSecondsFrac        data[2]
#define tSecondsInt         data[3]
#define tMinutes            data[4]
#define tHours              data[5]
#define tIsCountdown        data[6]
#define tUseAutomaticPause  data[7]
#define tIsTimerPaused      data[8]
#define tSpriteId_0         data[9]
#define tSpriteId_1         data[10]
#define tValue              data[11]
#define tCounterSec         data[12]
#define tCounterFrames      data[13]

void TimerMainTask(u8 taskId);
static void ShowTimer(u8 taskId);
static void UpdateTimer(u8 taskId);
static void TimeUp(u8 taskId);
static void CreateTimeSprites(u8 taskId);
static void DestroyTimeSprites(u8 taskId);
static void PrintTimer(u8 taskId);
static void HideTimerSprites(u8 taskId, u8 invisible);

static const u16 sTimerDigits_Pal[]     = INCBIN_U16("graphics/berry_crush/timer_digits.gbapal");
static const u32 sTimerDigits_Gfx[]     = INCBIN_U32("graphics/berry_crush/timer_digits.4bpp.lz");

static const struct CompressedSpriteSheet sSpriteSheets =
{
    sTimerDigits_Gfx, 0x2C0, TAG_TIMER_DIGITS
};

static const struct SpritePalette sSpritePals =
{
    sTimerDigits_Pal, TAG_TIMER_DIGITS
};

static const union AnimCmd sAnim_Timer[] =
{
    ANIMCMD_FRAME(20, 0),
    ANIMCMD_END
};

static const union AnimCmd *const sAnims_Timer[] =
{
    sAnim_Timer
};

static const struct SpriteTemplate sSpriteTemplate_Timer =
{
    .tileTag = TAG_TIMER_DIGITS,
    .paletteTag = TAG_TIMER_DIGITS,
    .oam = &gOamData_AffineOff_ObjNormal_8x16,
    .anims = sAnims_Timer,
    .images = NULL,
    .affineAnims = gDummySpriteAffineAnimTable,
    .callback = SpriteCallbackDummy
};

static const struct DigitObjUtilTemplate sDigitObjTemplates[] =
{
    { // 1/100ths of a second
        .strConvMode = 0,
        .shape = 2,
        .size = 0,
        .priority = 0,
        .oamCount = 2,
        .xDelta = 8,
        .x = 204,
        .y = 0,
        .spriteSheet = (void *) &sSpriteSheets,
        .spritePal = &sSpritePals,
    },
    { // Seconds
        .strConvMode = 0,
        .shape = 2,
        .size = 0,
        .priority = 0,
        .oamCount = 2,
        .xDelta = 8,
        .x = 180,
        .y = 0,
        .spriteSheet = (void *) &sSpriteSheets,
        .spritePal = &sSpritePals,
    },
    { // Minutes
        .strConvMode = 1,
        .shape = 2,
        .size = 0,
        .priority = 0,
        .oamCount = 2,
        .xDelta = 8,
        .x = 156,
        .y = 0,
        .spriteSheet = (void *) &sSpriteSheets,
        .spritePal = &sSpritePals,
    }
};

void InitTimer(void)
{  
    u8 taskId;
    
    if (FlagGet(FLAG_TIMER))
    {
        if (FindTaskIdByFunc(TimerMainTask) != TASK_NONE || FindTaskIdByFunc(ShowTimer) != TASK_NONE)
            return;
    }
    
    taskId = CreateTask(ShowTimer, 3);
    gTasks[taskId].tId = taskId;
    gTasks[taskId].tState = 0;
    gTasks[taskId].tSpriteId_0 = 0xFF;
    gTasks[taskId].tSpriteId_1 = 0xFF;
    if (FlagGet(FLAG_TIMER))
    {
        gTasks[taskId].tValue = VarGet(VAR_TIMER_VALUE);
        gTasks[taskId].tCounterSec = VarGet(VAR_TIMER_COUNTER_SEC);
        gTasks[taskId].tCounterFrames = VarGet(VAR_TIMER_COUNTER_FRAMES);
        gTasks[taskId].tIsCountdown = FlagGet(FLAG_IS_COUNTDOWN);
        gTasks[taskId].tUseAutomaticPause = FlagGet(FLAG_SCRIPT_PAUSE);
    }
    else
    {
        if (gSpecialVar_0x8000 <= MAX_TIME)
            gTasks[taskId].tValue = gSpecialVar_0x8000;
        else
            gTasks[taskId].tValue = MAX_TIME;
        gTasks[taskId].tIsCountdown = gSpecialVar_0x8001;
        gTasks[taskId].tUseAutomaticPause = gSpecialVar_0x8002;
        
        if (gTasks[taskId].tUseAutomaticPause)
            FlagSet(FLAG_SCRIPT_PAUSE);
        else
            FlagClear(FLAG_SCRIPT_PAUSE);
        if (gTasks[taskId].tIsCountdown)
        {
            gTasks[taskId].tCounterSec = gTasks[taskId].tValue;
            FlagSet(FLAG_IS_COUNTDOWN);
        }
        else
        {
            gTasks[taskId].tCounterSec = 0;
            FlagClear(FLAG_IS_COUNTDOWN);
        }
        VarSet(VAR_TIMER_VALUE, gTasks[taskId].tValue);
        VarSet(VAR_TIMER_COUNTER_SEC, gTasks[taskId].tCounterSec);
        VarSet(VAR_TIMER_COUNTER_FRAMES, 0);
        FlagSet(FLAG_TIMER);
    }
}

void TimerMainTask(u8 taskId)
{
    struct Task *task = &gTasks[taskId];
    
    if (task->tUseAutomaticPause)
    {
        if (ArePlayerFieldControlsLocked() == TRUE)
        {
            if(FindTaskIdByFunc(Task_ShowStartMenu) != TASK_NONE)
                HideTimerSprites(taskId, TRUE);
            return;
        }
        
    }
    else
    {
        if (task->tIsTimerPaused == TRUE)
        {
            return;
        }
        if(FindTaskIdByFunc(Task_ShowStartMenu) != TASK_NONE)
        {
            HideTimerSprites(taskId, TRUE);
            return;
        }        
    }
    UpdateTimer(taskId);
    HideTimerSprites(taskId, FALSE);
}

static void ShowTimer(u8 taskId)
{
    struct Task *task = &gTasks[taskId];
    
    switch (task->tState)
    {
    case 0:
        DestroyTimeSprites(taskId);
        break;
    case 1:
        CreateTimeSprites(taskId);
        break;
    case 2:
        task->func = TimerMainTask;
    }

    task->tState++;
}

static void UpdateTimer(u8 taskId)
{
    struct Task *task = &gTasks[taskId];
    
    if (task->tIsCountdown == TRUE)
    {
        --task->tCounterFrames;
        if (task->tCounterFrames < 0)
        {
            task->tCounterFrames = 59;
            --task->tCounterSec;
        }
        if (task->tCounterSec < 0)
        {
            TimeUp(taskId);
            return;
        }
    }
    else
    {
        ++task->tCounterFrames;
        if (task->tCounterFrames > 59)
        {
            task->tCounterFrames = 0;
            ++task->tCounterSec;
        }
        if (task->tCounterSec == task->tValue)
        {
            TimeUp(taskId);
            return;
        }
    }
    
    VarSet(VAR_TIMER_COUNTER_SEC, task->tCounterSec);
    VarSet(VAR_TIMER_COUNTER_FRAMES, task->tCounterFrames);
    PrintTimer(taskId);
}

void DestroyTimer(void)
{
    u8 taskId = FindTaskIdByFunc(TimerMainTask);
    if (taskId == TASK_NONE)
        taskId = FindTaskIdByFunc(ShowTimer);
    
    if (taskId != TASK_NONE)
    {
        DestroyTimeSprites(taskId);
        FlagClear(FLAG_TIMER);
        VarSet(VAR_TIMER_COUNTER_SEC, 0);
        VarSet(VAR_TIMER_COUNTER_FRAMES, 0);
        DestroyTask(taskId);
    }
}

static void TimeUp(u8 taskId)
{
    PrintTimer(taskId);
    PlaySE(SE_FAILURE);
    ScriptContext_SetupScript(gSaveBlock2Ptr->timerScriptPtr);
    DestroyTimer();
}

static void FramesToMinSec(u8 taskId)
{
    struct Task *task = &gTasks[taskId];
    
    task->tMinutes = task->tCounterSec / 60;
    task->tSecondsInt = task->tCounterSec % 60;
    task->tSecondsFrac = (task->tCounterFrames * 100) / 60;
}

static void PrintTimer(u8 taskId)
{
    struct Task *task = &gTasks[taskId];
    
    FramesToMinSec(taskId);
    DigitObjUtil_PrintNumOn(2, task->tMinutes);
    DigitObjUtil_PrintNumOn(1, task->tSecondsInt);
    DigitObjUtil_PrintNumOn(0, task->tSecondsFrac);
}

static void DestroyTimeSprites(u8 taskId)
{
    struct Task *task = &gTasks[taskId];
    
    if (task->tSpriteId_0 != 0xFF)
    {
        DestroySprite(&gSprites[task->tSpriteId_0]);
        task->tSpriteId_0 = 0xFF;
    }
    if (task->tSpriteId_1 != 0xFF)
    {
        DestroySprite(&gSprites[task->tSpriteId_1]);
        task->tSpriteId_1 = 0xFF;
    }
    
    FreeSpriteTilesByTag(TAG_TIMER_DIGITS);
    FreeSpritePaletteByTag(TAG_TIMER_DIGITS);
    DigitObjUtil_DeletePrinter(2);
    DigitObjUtil_DeletePrinter(1);
    DigitObjUtil_DeletePrinter(0);
    DigitObjUtil_Free();
}

static void HideTimerSprites(u8 taskId, u8 invisible)
{
    struct Task *task = &gTasks[taskId];
    
    if (task->tSpriteId_0 != 0xFF)
        gSprites[task->tSpriteId_0].invisible = invisible;
    if (task->tSpriteId_1 != 0xFF)
        gSprites[task->tSpriteId_1].invisible = invisible;
        
    DigitObjUtil_HideOrShow(2, invisible);
    DigitObjUtil_HideOrShow(1, invisible);
    DigitObjUtil_HideOrShow(0, invisible);
}

static void CreateTimeSprites(u8 taskId)
{
    struct Task *task = &gTasks[taskId];
    u8 i = 0;
    u8 spriteId;

    DigitObjUtil_Init(3);

    LoadCompressedSpriteSheet(&sSpriteSheets);
    LoadSpritePalettes(&sSpritePals);

    for (i = 0; i < 2; i++)
    {
        spriteId = CreateSprite(&sSpriteTemplate_Timer, 200 - 24 * i, 8, 0);
        gSprites[spriteId].oam.priority = 0;
        gSprites[spriteId].invisible = TRUE;
        gSprites[spriteId].animPaused = FALSE;
        
        if (i == 0)
            task->tSpriteId_0 = spriteId;
        else
            task->tSpriteId_1 = spriteId;
    }

    DigitObjUtil_CreatePrinter(0, 0, &sDigitObjTemplates[0]);
    DigitObjUtil_CreatePrinter(1, 0, &sDigitObjTemplates[1]);
    DigitObjUtil_CreatePrinter(2, 0, &sDigitObjTemplates[2]);

    HideTimerSprites(taskId, TRUE);
}

#undef tId
#undef tState
#undef tSecondsFrac
#undef tSecondsInt
#undef tMinutes
#undef tHours
#undef tIsCountdown
#undef tUseAutomaticPause
#undef tIsTimerPaused
#undef tSpriteId_0
#undef tSpriteId_1
#undef tValue
#undef tCounterSec
#undef tCounterFrames
