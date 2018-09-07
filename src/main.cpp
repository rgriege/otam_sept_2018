#include "gameDefines.h"
#if !DESKTOP
#include <OpenGLES/ES3/gl.h>
#else 
#include <GL/gl3w.h>
#endif


#if !DESKTOP
#include <sdl.h>
#include "SDL_syswm.h"
#else 
#include <SDL2/sdl.h>
#include <SDL2/SDL_syswm.h>
#endif

#include "easy_headers.h"

#include "easy_asset_loader.h"

#include "easy_transition.h"
#include "menu.h"

int EventFilter(void* userdata, SDL_Event* event)
{
    switch (event->type)
    {
        case SDL_FINGERMOTION:
            SDL_Log("Finger Motion");
            return 0;
            
        case SDL_FINGERDOWN:
            SDL_Log("Finger Down");
            return 0;
            
        case SDL_FINGERUP:
            SDL_Log("Finger Up");
            return 0;
    }
    
    return 1;
}

typedef enum {
    BOARD_NULL,
    BOARD_STATIC,
    BOARD_SHAPE,
    BOARD_EXPLOSIVE,
    BOARD_INVALID, //For out of bounds 
} BoardState;

#define MAX_SHAPE_COUNT 16
typedef struct {
    V2 coords[MAX_SHAPE_COUNT];
    GLBufferHandles renderHandle;
    int count;
    bool valid;

    Timer moveTimer;
} FitrisShape;

typedef enum {
    BOARD_VAL_NULL,
    BOARD_VAL_OLD,
    BOARD_VAL_ALWAYS,
    BOARD_VAL_TRANSIENT, //this isn't used for anything, just to make it so we aren't using the other ones. 
} BoardValType;

typedef struct {
    BoardValType type;
    BoardState state;
    BoardState prevState;

    V4 color;

    Timer fadeTimer;
} BoardValue;

typedef enum {
    LEVEL_0,
    LEVEL_1, 
    LEVEL_2, 
    LEVEL_3, 
} LevelType;

typedef struct {
    Arena *soundArena;
    int boardWidth;
    int boardHeight;
    BoardValue *board;
    GLBufferHandles bgBoardHandle;
    GLBufferHandles fgBoardHandle;

    FitrisShape currentShape;
    Texture *stoneTex;
    Texture *woodTex;
    Texture *bgTex;
    Texture *metalTex;
    Texture *explosiveTex;
    Texture *boarderTex;

    WavFile *solidfyShapeSound;
    WavFile *moveSound;
    WavFile *backgroundSound;
    WavFile *successSound;

    TransitionState transitionState;

    int lifePoints;
    int lifePointsMax;

    bool createShape;

    int currentBlockCount;
    LevelType currentLevelType;

    Timer moveTimer;

    int currentHotIndex;

    MenuInfo menuInfo;
    V2 screenRelativeSize;

    ////////
    Arena *longTermArena;
    float dt;
    SDL_Window *windowHandle;
    AppKeyStates *keyStates;
    FrameBuffer mainFrameBuffer;
    Matrix4 metresToPixels;
    Matrix4 pixelsToMeters;
    
    V2 *screenDim;
    V2 *resolution; 

    GLuint backbufferId;
    GLuint renderbufferId;

    Font *font;

    V3 cameraPos;

    unsigned int lastTime;
    ///////
    
} FrameParams;

typedef enum {
    MOVE_LEFT, 
    MOVE_RIGHT,
    MOVE_DOWN
} MoveType;



static inline float getRandNum01_include() {
    float result = ((float)rand() / (float)RAND_MAX);
    return result;
}

static inline float getRandNum01() {
    float result = ((float)rand() / (float)(RAND_MAX - 1));
    return result;
}


BoardState getBoardState(FrameParams *params, V2 pos) {
    BoardState result = BOARD_INVALID;
    if(pos.x >= 0 && pos.x < params->boardWidth && pos.y >= 0 && pos.y < params->boardHeight) {
        BoardValue *val = &params->board[params->boardWidth*(int)pos.y + (int)pos.x];
        result = val->state;
        assert(result != BOARD_INVALID);
    }
    
    return result;
}

BoardValue *getBoardValue(FrameParams *params, V2 pos) {
    BoardValue *result = 0;
    if(pos.x >= 0 && pos.x < params->boardWidth && pos.y >= 0 && pos.y < params->boardHeight) {
        BoardValue *val = &params->board[params->boardWidth*(int)pos.y + (int)pos.x];
        result = val;
    }
    
    return result;
}

void setBoardState(FrameParams *params, V2 pos, BoardState state, BoardValType type) {
    if(pos.x >= 0 && pos.x < params->boardWidth && pos.y >= 0 && pos.y < params->boardHeight) {
        BoardValue *val = &params->board[params->boardWidth*(int)pos.y + (int)pos.x];
        val->prevState = val->state;
        val->state = state;
        val->type = type;
        val->fadeTimer = initTimer(FADE_TIMER_INTERVAL);
    } else {
        assert(!"invalid code path");
    }
}

void createLevel(FrameParams *params, int blockCount, LevelType levelType) {
    for(int i = 0; i < blockCount && levelType != LEVEL_0; ++i) {
        V2 pos = {};
        float rand1 = getRandNum01_include();
        float rand2 = getRandNum01_include();
        pos.x = lerp(0, rand1, (float)(params->boardWidth - 1));
        pos.y = lerp(0, rand2, (float)(params->boardHeight - 5)); // so we don't block the shape creation

        BoardState state = BOARD_NULL;
        switch(levelType) {
            case LEVEL_1: {
                state = BOARD_STATIC;
            } break;
            case LEVEL_2: {
                int type = (int)lerp(0, getRandNum01(), 2);
                if(type == 0) { state = BOARD_STATIC; }
                if(type == 1) { state = BOARD_EXPLOSIVE; }
            } break;
            case LEVEL_3: {
                state = BOARD_EXPLOSIVE;
            } break;
            default: {
                assert(!"case not handled");
            }
        }
        
        setBoardState(params, v2((int)pos.x, (int)pos.y), state, BOARD_VAL_ALWAYS);    
    }
}

V2 getMoveVec(MoveType moveType) {
    V2 moveVec = v2(0, 0);
    if(moveType == MOVE_LEFT) {
        moveVec = v2(-1, 0);
    } else  if(moveType == MOVE_RIGHT) {
        moveVec = v2(1, 0);
    } else  if(moveType == MOVE_DOWN) {
        moveVec = v2(0, -1);
    } else {
        assert(!"not valid path");
    }
    return moveVec;
}

bool canShapeMove(FitrisShape *shape, FrameParams *params, MoveType moveType) {
    bool result = false;
    bool valid = true;
    int leftMostPos = params->boardWidth;
    int rightMostPos = 0;
    int bottomMostPos = params->boardHeight;;
    V2 moveVec = getMoveVec(moveType);
    for(int i = 0; i < shape->count; ++i) {
        V2 pos = shape->coords[i];
        BoardState state = getBoardState(params, v2_plus(pos, moveVec));
        if(!(state == BOARD_NULL || state == BOARD_SHAPE || state == BOARD_EXPLOSIVE)) {
            valid = false;
            break;
        }
        if(shape->coords[i].x < leftMostPos) {
            leftMostPos = shape->coords[i].x;
        }
        if(shape->coords[i].x > rightMostPos) {
            rightMostPos = shape->coords[i].x;
        }
        if(shape->coords[i].y < bottomMostPos) {
            bottomMostPos = shape->coords[i].y;
        }
    }

    //Check shape won't move off the board//
    if(moveType == MOVE_LEFT) {
        if(leftMostPos > 0 && valid) {
            result = true;
        }
    } else if(moveType == MOVE_RIGHT) {
        assert(rightMostPos < params->boardWidth);
        if(rightMostPos < (params->boardWidth - 1) && valid) {
            result = true;
        } 
    } else if(moveType == MOVE_DOWN) { 
        if(bottomMostPos > 0 && valid) {
            assert(bottomMostPos < params->boardHeight);
            result = true;
        }
    }
    return result;
}
bool isInShape(FitrisShape *shape, V2 pos) {
    bool result = false;
    for(int i = 0; i < shape->count; ++i) {
      V2 shapePos = shape->coords[i];
      if(pos.x == shapePos.x && pos.y == shapePos.y) {
        result = true;
        break;
      }
   }
   return result;
}

typedef struct {
    bool result;
    int index;
} QueryShapeInfo;

QueryShapeInfo isRepeatedInShape(FitrisShape *shape, V2 pos, int index) {
    QueryShapeInfo result = {};
    for(int i = 0; i < shape->count; ++i) {
      V2 shapePos = shape->coords[i];
      if(i != index && pos.x == shapePos.x && pos.y == shapePos.y) {
        result.result = true;
        result.index = i;
        assert(i != index);
        break;
      }
   }
   return result;
}

bool moveShape(FitrisShape *shape, FrameParams *params, MoveType moveType) {
    bool result = canShapeMove(shape, params, moveType);
    if(result) {
        // static int num = 0;
        // printf("%s%d\n", "CAN MOVE!!!!--------", num);
        // num++;
        bool alive = true;
       V2 moveVec = getMoveVec(moveType);
       for(int i = 0; i < shape->count; ++i) {
            V2 oldPos = shape->coords[i];
            V2 newPos = v2_plus(oldPos, moveVec);
           
            assert(getBoardState(params, oldPos) == BOARD_SHAPE);
            assert(getBoardState(params, newPos) == BOARD_SHAPE || getBoardState(params, newPos) == BOARD_NULL);

            QueryShapeInfo info = isRepeatedInShape(shape, oldPos, i);
            if(!info.result) { //dind't just get set by the block in shape before. 
                setBoardState(params, oldPos, BOARD_NULL, BOARD_VAL_TRANSIENT);    
            }
            setBoardState(params, newPos, BOARD_SHAPE, BOARD_VAL_TRANSIENT);    
            shape->coords[i] = newPos;
        }
        if(alive) {
            playSound(params->soundArena, params->moveSound, 0, AUDIO_FOREGROUND);
        }
    }
    return result;
}

void solidfyShape(FitrisShape *shape, FrameParams *params) {
    for(int i = 0; i < shape->count; ++i) {
        V2 pos = shape->coords[i];
        BoardValue *val = &params->board[params->boardWidth*(int)pos.y + (int)pos.x];
        assert(val->state == BOARD_SHAPE);

        setBoardState(params, pos, BOARD_STATIC, BOARD_VAL_OLD);
    }
    playSound(params->soundArena, params->solidfyShapeSound, 0, AUDIO_FOREGROUND);
}

typedef struct VisitedQueue VisitedQueue;
typedef struct VisitedQueue {
    V2 pos;
    VisitedQueue *next;
    VisitedQueue *prev;
} VisitedQueue;

bool shapeStillConnected(FitrisShape *shape, int currentHotIndex, V2 boardPosAt, FrameParams *params) {
    bool result = true;

    for(int i = 0; i < shape->count; ++i) {
        V2 pos = shape->coords[i];
        if(boardPosAt.x == pos.x && boardPosAt.y == pos.y) {
            result = false;
            break;
        }

        BoardState state = getBoardState(params, boardPosAt);
        if(state != BOARD_NULL) {
            result = false;
            break;
        }
    }
    if(result) {
        MemoryArenaMark mark = takeMemoryMark(params->longTermArena);
        bool *boardArray = pushArray(params->longTermArena, params->boardWidth*params->boardHeight, bool);

        VisitedQueue sentinel = {};
        sentinel.prev = sentinel.next = &sentinel;

        while(sentinel.next != &sentinel) {

        }

        releaseMemoryMark(&mark);
    }
    
    return result;
}

void updateAndRenderShape(FitrisShape *shape, V3 cameraPos, V2 resolution, V2 screenDim, Matrix4 metresToPixels, FrameParams *params) {
    if(wasPressed(gameButtons, BUTTON_LEFT)) {
        moveShape(shape, params, MOVE_LEFT);
    }
    if(wasPressed(gameButtons, BUTTON_RIGHT)) {
        moveShape(shape, params, MOVE_RIGHT);
    }
    if(wasPressed(gameButtons, BUTTON_DOWN)) {
        if(moveShape(shape, params, MOVE_DOWN)) {
            params->moveTimer.value = 0;
        }
    }

    TimerReturnInfo timerInfo = updateTimer(&params->moveTimer, params->dt);
    if(timerInfo.finished) {
        turnTimerOn(&params->moveTimer);
        if(!moveShape(shape, params, MOVE_DOWN)) {
            //if we are blocked 
            solidfyShape(shape, params);
            params->createShape = true;    
        }
    }

    if(wasReleased(gameButtons, BUTTON_LEFT_MOUSE)) {
        params->currentHotIndex = -1; //reset hot ui   
    }
    int hotBlockIndex = -1;
    for(int i = 0; i < shape->count; ++i) {
        V2 *pos = shape->coords +i;
        //UPDATE SHAPE//

        //RENDER SHAPE///
        
        RenderInfo renderInfo = calculateRenderInfo(v3(pos->x, pos->y, -1), v3(1, 1, 1), cameraPos, metresToPixels);

        Rect2f blockBounds = rect2fCenterDimV2(renderInfo.transformPos.xy, renderInfo.transformDim.xy);
        
        V4 color = COLOR_WHITE;
        
        if(inBounds(params->keyStates->mouseP_yUp, blockBounds, BOUNDS_RECT)) {
            hotBlockIndex = i;
            if(params->currentHotIndex < 0) {
                color = COLOR_YELLOW;
            }
        }
        if(params->currentHotIndex == i) {
            assert(isDown(gameButtons, BUTTON_LEFT_MOUSE));
            color = COLOR_GREEN;
        }
        BoardValue *val = getBoardValue(params, *pos);
        assert(val);
        val->color = color;

        // openGlTextureCentreDim(&shape->renderHandle, params->stoneTex->id, renderInfo.pos, renderInfo.dim.xy, color, 0, mat4(), 1, mat4(), Mat4Mult(OrthoMatrixToScreen(resolution.x, resolution.y, 1), renderInfo.pvm));                    
    }

    if(wasPressed(gameButtons, BUTTON_LEFT_MOUSE)) {
        params->currentHotIndex = hotBlockIndex;
    }
    
    if(params->currentHotIndex >= 0) {
        //We are holding onto a block
        V2 pos = params->keyStates->mouseP_yUp;
        
        V2 boardPosAt = V4MultMat4(v4(pos.x, pos.y, 1, 1), params->pixelsToMeters).xy;
        boardPosAt.x += cameraPos.x;
        boardPosAt.y += cameraPos.y;
        boardPosAt.x = (int)(clamp(0, boardPosAt.x, params->boardWidth - 1) + 0.5f);
        boardPosAt.y = (int)(clamp(0, boardPosAt.y, params->boardHeight -1) + 0.5f);

        if(shapeStillConnected(shape, params->currentHotIndex, boardPosAt, params)) {
            V2 oldPos = shape->coords[params->currentHotIndex];
            V2 newPos = boardPosAt;
            assert(getBoardState(params, oldPos) == BOARD_SHAPE);
            setBoardState(params, oldPos, BOARD_NULL, BOARD_VAL_TRANSIENT);    
            setBoardState(params, newPos, BOARD_SHAPE, BOARD_VAL_TRANSIENT);    
            shape->coords[params->currentHotIndex] = newPos;
        }

    }
}

Texture *getBoardTex(BoardValue *boardVal, BoardState boardState, FrameParams *params) {
    Texture *tex = 0;
    if(boardState != BOARD_NULL) {
        switch(boardState) {
            case BOARD_STATIC: {
                if(boardVal->type == BOARD_VAL_OLD) {
                    tex = params->metalTex;
                    assert(tex);
                } else if(boardVal->type == BOARD_VAL_ALWAYS) {
                    tex = params->woodTex;
                    assert(tex);
                } else {
                    assert(!"invalid path");
                }
                assert(tex);
            } break;
            case BOARD_EXPLOSIVE: {
                tex = params->explosiveTex;
            } break;
            case BOARD_SHAPE: {
                tex = params->stoneTex;
            } break;
            default: {
                assert(!"not handled");
            }
        }
        assert(tex);
    }
    return tex;
}

void updateBoardWinState(FrameParams *params) {
    for(int boardY = 0; boardY < params->boardHeight; ++boardY) {
        bool win = true;
        for(int boardX = 0; boardX < params->boardWidth; ++boardX) {
            BoardValue *boardVal = &params->board[boardY*params->boardWidth + boardX];
            if(boardVal->state == BOARD_NULL || boardVal->state == BOARD_SHAPE) {
                win = false;
                break;
            } 
            assert(boardVal->state != BOARD_INVALID);
        }
        if(win) {
            for(int boardX = 0; boardX < params->boardWidth; ++boardX) {
                BoardValue *boardVal = &params->board[boardY*params->boardWidth + boardX];
                if(boardVal->state == BOARD_STATIC && boardVal->type == BOARD_VAL_OLD) {
                    boardVal->fadeTimer = initTimer(FADE_TIMER_INTERVAL);
                    boardVal->prevState = boardVal->state;
                    boardVal->state = BOARD_NULL;
                }
            }
            playSound(params->soundArena, params->successSound, 0, AUDIO_FOREGROUND);
        }
    }
}

void initBoard(Arena *longTermArena, FrameParams *params, int boardWidth, int boardHeight, LevelType levelType, int blockCount, bool createArray) {
    params->boardWidth = boardWidth;
    params->boardHeight = boardHeight;

    params->lifePoints = params->lifePointsMax;
    if(createArray) {
        params->board = pushArray(longTermArena, params->boardWidth*params->boardHeight, BoardValue);
    } 

    for(int boardY = 0; boardY < params->boardHeight; ++boardY) {
        for(int boardX = 0; boardX < params->boardWidth; ++boardX) {
            BoardValue *boardVal = &params->board[boardY*params->boardWidth + boardX];

            boardVal->type = BOARD_VAL_NULL;
            boardVal->state = BOARD_NULL;
            boardVal->prevState = BOARD_NULL;

            boardVal->fadeTimer.value = -1;
            boardVal->color = COLOR_WHITE;
        }
    }

    createLevel(params, blockCount, levelType);
}

typedef struct {
    int blockCount;
    LevelType levelType;
    FrameParams *params;
} TransitionDataLevel;

void transitionCallbackForLevel(void *data_) {
    TransitionDataLevel *trans = (TransitionDataLevel *)data_;
    FrameParams *params = trans->params;

    initBoard(params->longTermArena, params, params->boardWidth, params->boardHeight, trans->levelType, trans->blockCount, false);
    params->createShape = true;   
} 

void setLevelTransition(FrameParams *params,  int blockCount, LevelType levelType) {
    TransitionDataLevel *data = (TransitionDataLevel *)calloc(sizeof(TransitionDataLevel), 1);
    data->blockCount = blockCount;
    data->levelType = levelType;
    data->params = params;
    setTransition_(&params->transitionState, transitionCallbackForLevel, data);
}

void gameUpdateAndRender(void *params_) {
    FrameParams *params = (FrameParams *)params_;
    V2 screenDim = *params->screenDim;
    V2 resolution = *params->resolution;
    V2 middleP = v2_scale(0.5f, resolution);
    //make this platform independent
    easyOS_beginFrame(resolution);
    //////CLEAR BUFFERS
    // 
    clearBufferAndBind(params->backbufferId, COLOR_BLACK);
    clearBufferAndBind(params->mainFrameBuffer.bufferId, COLOR_PINK);
    renderEnableDepthTest(&globalRenderGroup);
    static GLBufferHandles bgHandle = {};
    openGlTextureCentreDim(&bgHandle, params->bgTex->id, v2ToV3(middleP, -2), resolution, COLOR_WHITE, 0, mat4(), 1, mat4(), OrthoMatrixToScreen(resolution.x, resolution.y, 1));                    
    
    bool isPlayState = drawMenu(&params->menuInfo, params->longTermArena, gameButtons, 0, params->successSound, params->moveSound, params->dt, params->screenRelativeSize, params->keyStates->mouseP);
    bool transitioning = updateTransitions(&params->transitionState, resolution, params->dt);
    if(!transitioning && isPlayState) {
        //if updating a transition don't update the game logic, just render the game board. 
        if(params->createShape) {
            params->currentShape.count = 0;
            for (int i = 0; i < 4; ++i) {
                int xAt = i % params->boardWidth;
                int yAt = (params->boardHeight - 1) - (i / params->boardWidth);
                params->currentShape.coords[params->currentShape.count++] = v2(xAt, yAt);
                V2 pos = v2(xAt, yAt);
                if(getBoardState(params, pos) != BOARD_NULL) {
                    setLevelTransition(params, params->currentBlockCount, params->currentLevelType);
                    //
                } else {
                    setBoardState(params, pos, BOARD_SHAPE, BOARD_VAL_TRANSIENT);    
                }
            }
            params->moveTimer.value = 0;
            params->createShape = false;
        }

        updateAndRenderShape(&params->currentShape, params->cameraPos, resolution, screenDim, params->metresToPixels, params);
        updateBoardWinState(params);
    }

    if(isPlayState) {
        for(int boardY = 0; boardY < params->boardHeight; ++boardY) {
            for(int boardX = 0; boardX < params->boardWidth; ++boardX) {
                RenderInfo renderInfo = calculateRenderInfo(v3(boardX, boardY, -2), v3(1, 1, 1), params->cameraPos, params->metresToPixels);
                BoardValue *boardVal = &params->board[boardY*params->boardWidth + boardX];
                openGlTextureCentreDim(&params->bgBoardHandle, params->boarderTex->id, renderInfo.pos, renderInfo.dim.xy, COLOR_WHITE, 0, mat4(), 1, mat4(), Mat4Mult(OrthoMatrixToScreen(resolution.x, resolution.y, 1), renderInfo.pvm));            
                
                if(!(boardVal->prevState == BOARD_NULL && boardVal->state == BOARD_NULL)) {
                    V4 currentColor = boardVal->color;
                    if(isOn(&boardVal->fadeTimer)) {
                        TimerReturnInfo timeInfo = updateTimer(&boardVal->fadeTimer, params->dt);
                            
                        float lerpT = timeInfo.canonicalVal;
                        V4 prevColor = lerpV4(boardVal->color, clamp01(lerpT), COLOR_NULL);
                        currentColor = lerpV4(COLOR_NULL, lerpT, boardVal->color);

                        RenderInfo bgRenderInfo = calculateRenderInfo(v3(boardX, boardY, -1), v3(1, 1, 1), params->cameraPos, params->metresToPixels);

                        Texture *tex = getBoardTex(boardVal, boardVal->prevState, params);
                        if(tex) {
                            openGlTextureCentreDim(&params->fgBoardHandle, tex->id, bgRenderInfo.pos, renderInfo.dim.xy, prevColor, 0, mat4(), 1, mat4(), Mat4Mult(OrthoMatrixToScreen(resolution.x, resolution.y, 1), renderInfo.pvm));            
                        }    

                        if(timeInfo.finished) {
                            boardVal->prevState = boardVal->state;
                        }
                    }
                        
                    Texture *tex = getBoardTex(boardVal, boardVal->state, params);
                    if(tex) {
                        openGlTextureCentreDim(&params->fgBoardHandle, tex->id, renderInfo.pos, renderInfo.dim.xy, currentColor, 0, mat4(), 1, mat4(), Mat4Mult(OrthoMatrixToScreen(resolution.x, resolution.y, 1), renderInfo.pvm));            
                    }
                } else {
                    assert(!isOn(&boardVal->fadeTimer));
                }
            }
        }
    }

   drawRenderGroup(&globalRenderGroup);
   
   easyOS_endFrame(resolution, screenDim, &params->dt, params->windowHandle, params->mainFrameBuffer.bufferId, params->backbufferId, params->renderbufferId, &params->lastTime, 1.0f / 60.0f);



}

int main(int argc, char *args[]) {
	
	V2 screenDim = {}; //init in create app function
	V2 resolution = v2(1280, 720); //this could be variable -> passed in by the app etc. 

  OSAppInfo appInfo = easyOS_createApp("Fitris", &screenDim);
  assert(appInfo.valid);
    
  if(appInfo.valid) {
    AppSetupInfo setupInfo = easyOS_setupApp(resolution, "../res/");

    float dt = 1.0f / min((float)setupInfo.refresh_rate, 60.0f); //use monitor refresh rate 
    float idealFrameTime = 1.0f / 60.0f;

    ////INIT FONTS
    Font mainFont = initFont_(concat(globalExeBasePath, "fonts/Merriweather-Regular.ttf"), 0, 2048, 32);
    ///

    Arena soundArena = createArena(Megabytes(200));
    Arena longTermArena = createArena(Megabytes(200));

    loadAndAddImagesToAssets("img/");
    loadAndAddSoundsToAssets("sounds/", &setupInfo.audioSpec);

    Texture *stoneTex = findTextureAsset("elementStone023.png");
    Texture *woodTex = findTextureAsset("elementWood022.png");
    Texture *bgTex = findTextureAsset("blue_grass.png");
    Texture *metalTex = findTextureAsset("elementMetal023.png");
    Texture *explosiveTex = findTextureAsset("elementExplosive049.png");
    Texture *boarderTex = findTextureAsset("elementMetal030.png");
    assert(metalTex);
    bool running = true;
      
    FrameParams params = {};
    params.solidfyShapeSound = findSoundAsset("slate_sound.wav");
    params.successSound = findSoundAsset("Success2.wav");
    
    params.moveSound = findSoundAsset("menuSound.wav");
    params.backgroundSound = findSoundAsset("Illusionist Finale.wav");

    //Play and repeat background sound
    PlayingSound *sound = playSound(&soundArena, params.backgroundSound, 0, AUDIO_FOREGROUND);
    sound->nextSound = sound;
    //

    params.soundArena = &soundArena;
    params.longTermArena = &longTermArena;
    params.dt = dt;
    params.windowHandle = appInfo.windowHandle;
    params.backbufferId = appInfo.frameBackBufferId;
    params.renderbufferId = appInfo.renderBackBufferId;
    params.mainFrameBuffer = createFrameBuffer(resolution.x, resolution.y, FRAMEBUFFER_DEPTH | FRAMEBUFFER_STENCIL);
    params.resolution = &resolution;
    params.screenDim = &screenDim;
    params.metresToPixels = setupInfo.metresToPixels;
    params.pixelsToMeters = setupInfo.pixelsToMeters;
    AppKeyStates keyStates = {};
    params.keyStates = &keyStates;
    params.font = &mainFont;
    params.screenRelativeSize = setupInfo.screenRelativeSize;
        
    int blockCount = 4;
    initBoard(&longTermArena, &params, BOARD_WIDTH, BOARD_HEIGHT, START_LEVEL, blockCount, true);
    params.currentBlockCount = blockCount;
    params.currentLevelType = START_LEVEL; //this is from the defines file
    params.lifePointsMax = 1;
    params.lifePoints = params.lifePointsMax;
    
    params.createShape = true;
    params.moveTimer = initTimer(1.0f);
    params.woodTex = woodTex;
    params.stoneTex = stoneTex;
    params.metalTex = metalTex;
    params.explosiveTex = explosiveTex;
    params.boarderTex = boarderTex;
    params.bgTex = bgTex;
    params.lastTime = SDL_GetTicks();
    params.currentHotIndex = -1;
    params.cameraPos = v3(-5, -2, 0);

    TransitionState transState = {};
    transState.transitionSound = findSoundAsset("click.wav");
    transState.soundArena = &soundArena;
    transState.longTermArena = &longTermArena;

    params.transitionState = transState;

    MenuInfo menuInfo = {};
    menuInfo.font = &mainFont;
    menuInfo.windowHandle = appInfo.windowHandle; 
    menuInfo.running = &running;
    menuInfo.lastMode = menuInfo.gameMode = START_MENU_MODE; //from the defines file
    menuInfo.transitionState = &params.transitionState;
    params.menuInfo = menuInfo;
        
    //

#if !DESKTOP    
    if(SDL_iPhoneSetAnimationCallback(appInfo.windowHandle, 1, gameUpdateAndRender, &params) < 0) {
    	assert(!"falid to set");
    }
#endif
    // if(SDL_AddEventWatch(EventFilter, NULL) < 0) {
    // 	assert(!"falid to set");
    // }
    
    while(running) {
        
    	keyStates = easyOS_processKeyStates(resolution, &screenDim, &running);
#if DESKTOP
      gameUpdateAndRender(&params);
#endif
    }
    easyOS_endProgram(&appInfo);
	}
}