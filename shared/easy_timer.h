typedef struct {
    float value;
    float period;
} Timer;

Timer initTimer(float period) {
    Timer result = {};
    result.period = period;
    return result;
}

bool isOn(Timer *timer) {
    bool result = timer->value >= 0;
    return result;
}

void turnTimerOn(Timer *timer) {
    timer->value = 0;
}

void turnTimerOff(Timer *timer) {
    timer->value = -1;
}

float getTimerValue01(Timer *timer) {
    float value = timer->value / timer->period;
    return value;
}
typedef struct {
    bool finished; 
    float canonicalVal;
} TimerReturnInfo;

TimerReturnInfo updateTimer(Timer *timer, float dt) {
    TimerReturnInfo returnInfo = {};
    if(timer->value >= 0.0f) {
        timer->value += dt;
        if(timer->period == 0) {
            float defaultPeriod = 1; 
            timer->period = defaultPeriod;
        }
        if((timer->value / timer->period) >= 1.0f) {
            timer->value = -1; //turn timer off
            returnInfo.canonicalVal = 1; //anyone using this value afterwards wants to know that it finished
            returnInfo.finished = true;
        } else {
            returnInfo.canonicalVal = getTimerValue01(timer);
        }
    }
    return returnInfo; 
}

#define LERP_TYPE(FUNC) \
FUNC(LINEAR) \
FUNC(SMOOTH_STEP_01) \
FUNC(SMOOTH_STEP_00) \
FUNC(SMOOTH_STEP_01010) \

typedef enum {
    LERP_TYPE(ENUM)
} LerpType;

static char *LerpTypeStrings[] = { LERP_TYPE(STRING) };

typedef struct {
    float a;
    float b;

    int lineNumber;
    
    float value;
    float *val;
    Timer timer;
} Lerpf;

typedef struct {
    V4 a;
    V4 b;

    int lineNumber;
    
    V4 value; //can point to this if it wants
    V4 *val;
    Timer timer;
} LerpV4;


typedef struct {
    V3 a;
    V3 b;

    int lineNumber;
    
    V3 value;
    V3 *val;
    Timer timer;
} LerpV3;

//TODO: change this to its own TimerVarType
typedef enum {
    VAR_TIMER_FLOAT,
    VAR_TIMER_V2,
    VAR_TIMER_V3,
    VAR_TIMER_V4,
} TimerVarType;

float updateLerpf_(float tAt, Lerpf *f, LerpType lerpType, TimerReturnInfo timeInfo) {
    
    float finishingValue = f->b;
    float lerpValue = 0;
    switch(lerpType) {
        case LINEAR: {
            lerpValue = lerp(f->a, tAt, f->b);
        } break;
        case SMOOTH_STEP_00: {
            lerpValue = smoothStep00(f->a, tAt, f->b);
            finishingValue = f->a;
        } break;
        case SMOOTH_STEP_01: {
            lerpValue = smoothStep01(f->a, tAt, f->b);
        } break;
        default: {
            invalidCodePathStr("Unhandled case in update lerp function\n");
        }
    }
    
    if(timeInfo.finished) {
        lerpValue = finishingValue;
        f->timer.value = -1;
    }
    return lerpValue;
}

V4 updateLerpV4_(float tAt, LerpV4 *f, LerpType lerpType, TimerReturnInfo timeInfo) {
    
    V4 lerpValue = {};
    V4 finishingValue = f->b;
    switch(lerpType) {
        case LINEAR: {
            lerpValue = lerpV4(f->a, tAt, f->b);
        } break;
        case SMOOTH_STEP_00: {
            lerpValue = smoothStep00V4(f->a, tAt, f->b);
            finishingValue = f->a;
        } break;
        case SMOOTH_STEP_01: {
            lerpValue = smoothStep01V4(f->a, tAt, f->b);
        } break;
        case SMOOTH_STEP_01010: {
            lerpValue = smoothStep01010V4(f->a, tAt, f->b);
            finishingValue = f->a;
        } break;
        default: {
            invalidCodePathStr("Unhandled case in update lerp function\n");
        }
    }
    
    if(timeInfo.finished) {
        lerpValue = finishingValue;
        f->timer.value = -1;
    }
    return lerpValue;
}

V3 updateLerpV3_(float tAt, LerpV3 *f, LerpType lerpType, TimerReturnInfo timeInfo) {
    
    V3 lerpValue = {};
    V3 finishingValue = f->b;
    switch(lerpType) {
        case LINEAR: {
            lerpValue = lerpV3(f->a, tAt, f->b);
        } break;
        case SMOOTH_STEP_00: {
            lerpValue = smoothStep00V3(f->a, tAt, f->b);
            finishingValue = f->a;
        } break;
        case SMOOTH_STEP_01: {
            lerpValue = smoothStep01V3(f->a, tAt, f->b);
        } break;
        default: {
            invalidCodePathStr("Unhandled case in update lerp function\n");
        }
    }
    
    if(timeInfo.finished) {
        lerpValue = finishingValue;
        f->timer.value = -1;
    }
    return lerpValue;
}

void updateLerpGeneral_(void *lerpStruct, Timer *timer, float dt, void *valPrt, LerpType lerpType, TimerVarType varType) {
    if(timer->value >= 0 && valPrt) { 
        TimerReturnInfo timeInfo = updateTimer(timer, dt);
        
        switch(varType) {
            case VAR_TIMER_FLOAT: {
                Lerpf *f = (Lerpf *)lerpStruct;
                *(f->val) = updateLerpf_(timeInfo.canonicalVal, f, lerpType, timeInfo);
            } break;
            case VAR_TIMER_V4: {
                LerpV4 *f = (LerpV4 *)lerpStruct;
                *(f->val) = updateLerpV4_(timeInfo.canonicalVal, f, lerpType, timeInfo);
            } break;
            case VAR_TIMER_V3: {
                LerpV3 *f = (LerpV3 *)lerpStruct;
                *(f->val) = updateLerpV3_(timeInfo.canonicalVal, f, lerpType, timeInfo);
            } break;
            default: {
                invalidCodePathStr("Unhandled case in update lerp function\n");
            }
        }
    }
}

void updateLerpf(Lerpf *f, float dt, LerpType lerpType) {
    updateLerpGeneral_(f, &f->timer, dt, f->val, lerpType, VAR_TIMER_FLOAT);
}


bool updateLerpV4(LerpV4 *f, float dt, LerpType lerpType) {
    bool wasTimerOn = isOn(&f->timer);
    updateLerpGeneral_(f, &f->timer, dt, f->val, lerpType, VAR_TIMER_V4);
    bool isTimerOn = isOn(&f->timer);

    bool result = false;
    if(wasTimerOn && !isTimerOn) {
        result = true;
    }
    return result;
}

void updateLerpV3(LerpV3 *f, float dt, LerpType lerpType) {
    updateLerpGeneral_(f, &f->timer, dt, f->val, lerpType, VAR_TIMER_V3);
}

void setLerpInfof_(Lerpf *f, float a, float b, float period, float *val, int lineNumber) {
    if(lineNumber != f->lineNumber || f->timer.value < 0) {
        f->a = a;
        f->b = b;
        f->lineNumber = lineNumber;

        f->timer = initTimer(period);
        f->val = val;
    }
}


void setLerpInfof_s_(Lerpf *f, float b, float period, float *val, int lineNumber) {
    if(lineNumber != f->lineNumber || f->timer.value < 0) {
        f->a = *val;
        f->b = b;
        f->lineNumber = lineNumber;
        f->timer = initTimer(period);
        f->val = val;
    }
}

#define setLerpInfof(f, a, b, period, val) setLerpInfof_(f, a, b, period, val, __LINE__) 

#define setLerpInfof_s(f, b, period, val) setLerpInfof_s_(f, b, period, val, __LINE__) 


void setLerpInfoV4_s_(LerpV4 *f, V4 b, float period, V4 *val, int lineNumber) {
    if(lineNumber != f->lineNumber || f->timer.value < 0) {
        f->a = *val;
        f->b = b;
        f->lineNumber = lineNumber;
        f->timer = initTimer(period);
        f->val = val;
    }
}

#define setLerpInfoV4_s(f, b, period, val) setLerpInfoV4_s_(f, b, period, val, __LINE__) 

void setLerpV4(LerpV4 *ler, V4 a, V4 b, float period) {
    ler->value = a;
    setLerpInfoV4_s(ler, b, period, &ler->value);
}

void setLerpInfoV3_s_(LerpV3 *f, V3 b, float period, V3 *val, int lineNumber) {
    if(lineNumber != f->lineNumber || f->timer.value < 0) {
        f->a = *val;
        f->b = b;
        f->lineNumber = lineNumber;
        f->timer = initTimer(period);
        f->val = val;
    }
}

#define setLerpInfoV3_s(f, b, period, val) setLerpInfoV3_s_(f, b, period, val, __LINE__) 


LerpV4 initLerpV4() {
    LerpV4 a = {};
    a.timer.value = -1; //turn off timer;
    return a;
}

LerpV3 initLerpV3() {
    LerpV3 a = {};
    a.timer.value = -1; //turn off timer;
    return a;
}

Lerpf initLerpf() {
    Lerpf a = {};
    a.timer.value = -1; //turn off timer;
    return a;
}

