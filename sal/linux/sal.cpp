#include <stdio.h>
#include <dirent.h>
#include <SDL.h>
#include <sys/time.h>
#include "sal.h"
#include "memmap.h"
#include <iostream>
#include "menu.h"

#define PALETTE_BUFFER_LENGTH    256*2*4
#define SNES_WIDTH  256
//#define SNES_HEIGHT 239

extern u16 IntermediateScreen[];
SDL_Surface *mScreen = NULL;
static u32 mSoundThreadFlag = 0;
static u32 mSoundLastCpuSpeed = 0;
static u32 mPaletteBuffer[PALETTE_BUFFER_LENGTH];
static u32 *mPaletteCurr = (u32 *) &mPaletteBuffer[0];
static u32 *mPaletteLast = (u32 *) &mPaletteBuffer[0];
static u32 *mPaletteEnd = (u32 *) &mPaletteBuffer[PALETTE_BUFFER_LENGTH];
static u32 mInputFirst = 0;

extern struct MENU_OPTIONS mMenuOptions;
s32 mCpuSpeedLookup[1] = {0};

#include <sal_common.h>

static u32 inputHeld = 0;

static u32 sal_Input(int held) {
    SDL_Event event;
    if (!SDL_PollEvent(&event)) {
        if (held) return inputHeld;
        return 0;
    }

    u32 extraKeys = 0;
    do {
        switch (event.type) {
            case SDL_KEYDOWN:
                switch (event.key.keysym.sym) {
#ifdef RG350
                    case SDLK_HOME:
#endif
#ifdef PG
                    case SDLK_RCTRL:
#endif
                    extraKeys |= SAL_INPUT_MENU;
                    break;
                }
                break;
        }
    } while (SDL_PollEvent(&event));

    inputHeld = 0;

    u8 *keystate = SDL_GetKeyState(NULL);
    if (keystate[SDLK_LCTRL]) inputHeld |= SAL_INPUT_A;
    if (keystate[SDLK_LALT]) inputHeld |= SAL_INPUT_B;
    if (keystate[SDLK_SPACE]) inputHeld |= SAL_INPUT_X;
    if (keystate[SDLK_LSHIFT]) inputHeld |= SAL_INPUT_Y;
    if (keystate[SDLK_TAB]) inputHeld |= SAL_INPUT_L;
    if (keystate[SDLK_BACKSPACE]) inputHeld |= SAL_INPUT_R;
    if (keystate[SDLK_RETURN]) inputHeld |= SAL_INPUT_START;
    if (keystate[SDLK_ESCAPE]) inputHeld |= SAL_INPUT_SELECT;
    if (keystate[SDLK_UP]) inputHeld |= SAL_INPUT_UP;
    if (keystate[SDLK_DOWN]) inputHeld |= SAL_INPUT_DOWN;
    if (keystate[SDLK_LEFT]) inputHeld |= SAL_INPUT_LEFT;
    if (keystate[SDLK_RIGHT]) inputHeld |= SAL_INPUT_RIGHT;
#ifdef RG350
    if ( keystate[SDLK_HOME] )		inputHeld |= SAL_INPUT_MENU;
#endif
#ifdef PG
    if ( keystate[SDLK_RCTRL] )		inputHeld |= SAL_INPUT_MENU;
#endif
    mInputRepeat = inputHeld;
    return inputHeld | extraKeys;
}

static int key_repeat_enabled = 1;

u32 sal_InputPollRepeat() {
    if (!key_repeat_enabled) {
        SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY,
                            SDL_DEFAULT_REPEAT_INTERVAL);
        key_repeat_enabled = 1;
    }
    return sal_Input(0);
}

u32 sal_InputPoll() {
    if (key_repeat_enabled) {
        SDL_EnableKeyRepeat(0, 0);
        key_repeat_enabled = 0;
    }
    return sal_Input(1);
}

const char *sal_DirectoryGetTemp(void) {
    return "/tmp";
}

void sal_CpuSpeedSet(u32 mhz) {
}

u32 sal_CpuSpeedNext(u32 currSpeed) {
    u32 newSpeed = currSpeed + 1;
    if (newSpeed > 500) newSpeed = 500;
    return newSpeed;
}

u32 sal_CpuSpeedPrevious(u32 currSpeed) {
    u32 newSpeed = currSpeed - 1;
    if (newSpeed > 500) newSpeed = 0;
    return newSpeed;
}

u32 sal_CpuSpeedNextFast(u32 currSpeed) {
    u32 newSpeed = currSpeed + 10;
    if (newSpeed > 500) newSpeed = 500;
    return newSpeed;
}

u32 sal_CpuSpeedPreviousFast(u32 currSpeed) {
    u32 newSpeed = currSpeed - 10;
    if (newSpeed > 500) newSpeed = 0;
    return newSpeed;
}

s32 sal_Init(void) {
    setenv("SDL_NOMOUSE", "1", 1);
//    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_NOPARACHUTE) == -1)
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_JOYSTICK |
                 SDL_INIT_NOPARACHUTE) == -1) {
        return SAL_ERROR;
    }
    sal_TimerInit(60);

    memset(mInputRepeatTimer, 0, sizeof(mInputRepeatTimer));

    SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);

    return SAL_OK;
}

u32 sal_VideoInit(u32 bpp) {
    SDL_ShowCursor(0);

    mBpp = bpp;

    //Set up the screen
    mScreen = SDL_SetVideoMode(SAL_SCREEN_WIDTH, SAL_SCREEN_HEIGHT, bpp,
                               SDL_HWSURFACE |
                               #ifdef SDL_TRIPLEBUF
                               SDL_TRIPLEBUF
                               #else
                               SDL_DOUBLEBUF
#endif
                              );

    //If there was an error in setting up the screen
//    if (mScreen == NULL) {
//        sal_LastErrorSet("SDL_SetVideoMode failed");
//        return SAL_ERROR;
//    }
    if (!mScreen) {
#ifdef MAKLOG
        std::cout << "sal.cpp:176" << " " << "setVideoMode fail : "
                  << SDL_GetError() << std::endl;
#endif
        exit(0);
    }

    return SAL_OK;
}

u32 sal_VideoGetWidth() {
    return mScreen->w;
}

u32 sal_VideoGetHeight() {
    return mScreen->h;
}

u32 sal_VideoGetPitch() {
    return mScreen->pitch;
}

extern int currentWidth;


void sal_VideoEnterGame(u32 fullscreenOption,
                        u32 pal,
                        u32 refreshRate) {
//#ifdef GCW_ZERO
//    /* Copied from C++ headers which we can't include in C */
//    unsigned int Width = 256 /* SNES_WIDTH */,
//            Height = pal ? 239 /* SNES_HEIGHT_EXTENDED */
//                         : 224 /* SNES_HEIGHT */;
//    if (fullscreenOption != 3) {
//        Width = SAL_SCREEN_WIDTH;
//        Height = SAL_SCREEN_HEIGHT;
//    }
//    if (SDL_MUSTLOCK(mScreen)) SDL_UnlockSurface(mScreen);
//    mScreen = SDL_SetVideoMode(Width, Height, mBpp, SDL_HWSURFACE |
//                                                    #ifdef SDL_TRIPLEBUF
//                                                    SDL_TRIPLEBUF
//                                                    #else
//                                                    SDL_DOUBLEBUF
//#endif
//                              );
    mRefreshRate = refreshRate;
//    if (SDL_MUSTLOCK(mScreen)) SDL_LockSurface(mScreen);

    updateVideoMode(false);
//#endif
}

void sal_VideoSetPAL(u32 fullscreenOption,
                     u32 pal) {
//    if (fullscreenOption == 3) /* hardware scaling */
//    {
//        sal_VideoEnterGame(fullscreenOption, pal, mRefreshRate);
//    }
    updateVideoMode(true);
}

void sal_VideoExitGame() {
#ifdef GCW_ZERO
    if (SDL_MUSTLOCK(mScreen)) SDL_UnlockSurface(mScreen);
    mScreen = SDL_SetVideoMode(SAL_SCREEN_WIDTH, SAL_SCREEN_HEIGHT, mBpp,
                               SDL_HWSURFACE |
                               #ifdef SDL_TRIPLEBUF
                               SDL_TRIPLEBUF
                               #else
                               SDL_DOUBLEBUF
#endif
                              );
    if (SDL_MUSTLOCK(mScreen)) SDL_LockSurface(mScreen);
#endif
}

void sal_VideoBitmapDim(u16 *img,
                        u32 pixelCount) {
    u32 i;
    for (i = 0; i < pixelCount; i += 2)
        *(u32 *) &img[i] = (*(u32 *) &img[i] & 0xF7DEF7DE) >> 1;
    if (pixelCount & 1)
        img[i - 1] = (img[i - 1] & 0xF7DE) >> 1;
}

void sal_VideoFlip(s32 vsync) {
    if (SDL_MUSTLOCK(mScreen)) SDL_UnlockSurface(mScreen);
    SDL_Flip(mScreen);
    if (SDL_MUSTLOCK(mScreen)) SDL_LockSurface(mScreen);
}

void *sal_VideoGetBuffer() {
    return (void *) mScreen->pixels;
}

void sal_VideoPaletteSync() {

}

void sal_VideoPaletteSet(u32 index,
                         u32 color) {
    *mPaletteCurr++ = index;
    *mPaletteCurr++ = color;
    if (mPaletteCurr > mPaletteEnd) mPaletteCurr = &mPaletteBuffer[0];
}

void sal_Reset(void) {
    sal_AudioClose();
    SDL_Quit();
}


static unsigned int currentMode = 3;

void updateVideoMode(bool force) {
//    GFX.Screen = (uint8 *) IntermediateScreen;

    if (!force && mMenuOptions.fullScreen == currentMode) {
        if (mMenuOptions.fullScreen == 3 || mMenuOptions.fullScreen == 0) {
            if (IPPU.RenderedScreenWidth == mScreen->w &&  GFX.RealPitch == IPPU.RenderedScreenWidth * sizeof(u16) ) {
                return;
            }

        } else {
            if (GFX.RealPitch == IPPU.RenderedScreenWidth * sizeof(u16)){
                return;
            }
        }
    }

    sal_VideoClear(0);
    sal_VideoClear(0);
    sal_VideoClear(0);


    switch (mMenuOptions.fullScreen) {
        case 0: // origin
            updateWindowSize(IPPU.RenderedScreenWidth, 240, 0);
//            GFX.Screen = (uint8 *) mScreen->pixels;
            break;
        case 1: // software fast
            updateWindowSize(320, 240, 1);
//            GFX.Screen = (uint8 *) IntermediateScreen;
            break;
        case 2: // software smooth
            updateWindowSize(320, 240, 1);
            break;
        case 3: // hardware
            updateWindowSize(IPPU.RenderedScreenWidth, 240, 0);
//            GFX.Screen = (uint8 *) mScreen->pixels;
            break;
    }
    GFX.Screen = (uint8 *) IntermediateScreen;
    currentMode = mMenuOptions.fullScreen;


}

void updateWindowSize(int width,
                      int height,
                      int isSoftware) {

//    if (mScreen->w == width) return;
//    if (isSoftware) {

    GFX.RealPitch = GFX.Pitch = IPPU.RenderedScreenWidth * sizeof(u16);
//    } else {
//
//        GFX.RealPitch = GFX.Pitch = width * sizeof(u16);
//    }

    GFX.SubScreen = (uint8 *) malloc(GFX.RealPitch * 480 * 2);
    GFX.ZBuffer = (uint8 *) malloc(GFX.RealPitch * 480 * 2);
    GFX.SubZBuffer = (uint8 *) malloc(GFX.RealPitch * 480 * 2);
    GFX.Delta = (GFX.SubScreen - GFX.Screen) >> 1;
    GFX.PPL = GFX.Pitch >> 1;
    GFX.PPLx2 = GFX.Pitch;
    GFX.ZPitch = GFX.Pitch >> 1;

    bool8 PAL = !!(Memory.FillRAM[0x2133] & 4);

#ifdef GCW_ZERO
    /* Copied from C++ headers which we can't include in C */
    unsigned int Width = width /* SNES_WIDTH */,
            Height = PAL ? 239 /* SNES_HEIGHT_EXTENDED */
                         : 224 /* SNES_HEIGHT */;
                         if (isSoftware) Height = 240;

    if (mScreen && SDL_MUSTLOCK(mScreen))
        SDL_UnlockSurface(mScreen);
    mScreen = SDL_SetVideoMode(Width, Height, mBpp, SDL_HWSURFACE |
                                                    #ifdef SDL_TRIPLEBUF
                                                    SDL_TRIPLEBUF
                                                    #else
                                                    SDL_DOUBLEBUF
#endif

                              );
    if (!mScreen) {
//        puts("SDL_SetVideoMode error" + SDL_GetError());
        std::cout << "SDL_SetVideoMode error :" << SDL_GetError() << std::endl;
        exit(0);
    }
    mRefreshRate = Memory.ROMFramesPerSecond;
    if (SDL_MUSTLOCK(mScreen))
        SDL_LockSurface(mScreen);

//    GFX.Screen = (uint8 *) mScreen->pixels;
#endif

}

// Prove entry point wrapper
int main(int argc,
         char *argv[]) {
    return mainEntry(argc, argv);
//	return mainEntry(argc-1,&argv[1]);
}
