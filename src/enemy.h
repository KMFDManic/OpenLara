#ifndef H_ENEMY
#define H_ENEMY

#include "character.h"

struct Enemy : Character {

    struct Path {
        int16       index;
        int16       count;
        uint16      *boxes;
        TR::Level   *level;

        Path(TR::Level *level, uint16 *boxes, int count) : index(0), count(count), boxes(new uint16[count]), level(level) {
            memcpy(this->boxes, boxes, count * sizeof(boxes[0]));
        }

        ~Path() {
            delete[] boxes;
        }

        bool getNextPoint(TR::Level *level, vec3 &point) {
            if (index >= count - 1)
                return false;

            TR::Box &a = level->boxes[boxes[index++]];
            TR::Box &b = level->boxes[boxes[index]];

            int minX = max(a.minX, b.minX);
            int minZ = max(a.minZ, b.minZ);
            int maxX = min(a.maxX, b.maxX);
            int maxZ = min(a.maxZ, b.maxZ);

            point.x = float(minX + 512) + randf() * (maxX - minX - 1024);
            point.y = float((a.floor + b.floor) / 2);
            point.z = float(minZ + 512) + randf() * (maxZ - minZ - 1024);

            return true;
        }
    };

    enum AI {
        AI_FIXED, AI_RANDOM
    } ai;

    enum Mood {
        MOOD_SLEEP, MOOD_STALK, MOOD_ATTACK, MOOD_ESCAPE 
    } mood;

    bool  wound;
    int   nextState;

    int   targetBox;
    vec3  waypoint;

    float thinkTime;
    float aggression;
    int   radius;
    int   stepHeight;
    int   dropHeight;

    Character *target;
    Path      *path;

    int jointChest;
    int jointHead;

    Enemy(IGame *game, int entity, int health, int radius, float aggression) : Character(game, entity, health), ai(AI_RANDOM), mood(MOOD_SLEEP), wound(false), nextState(0), targetBox(-1), thinkTime(0.0f), aggression(aggression), radius(radius), target(NULL), path(NULL) {
        stepHeight =  256;
        dropHeight = -256;

        jointChest = jointHead = -1;
    }

    virtual ~Enemy() {
        delete path;
    }

    virtual bool activate(ActionCommand *cmd) {
    #ifdef LEVEL_EDITOR
        return true;
    #endif

        Controller::activate(cmd);

        getEntity().flags.active = true;
        activateNext();

        for (int i = 0; i < level->entitiesCount; i++)
            if (level->entities[i].type == TR::Entity::LARA) {
                target = (Character*)level->entities[i].controller;
                break;
            }
        ASSERT(target);

        return true;
    }

    virtual void updateVelocity() {
        velocity = getDir() * animation.getSpeed();
    }

    bool checkPoint(int x, int z) {
        TR::Box &a = level->boxes[box];
        if (a.contains(x, z))
            return true;

        TR::Overlap *o = &level->overlaps[a.overlap.index];
        do {
            TR::Box &b = level->boxes[o->boxIndex];
            if (!b.contains(x, z))
                continue;
            if (getZones()[o->boxIndex] == zone) {
                int d = a.floor - b.floor;
                if (d <= stepHeight && d >= dropHeight)
                    return true;
            }
        } while (!(o++)->end);

        return false;
    }

    void clipByBox(vec3 &pos) {
        int px = int(pos.x);
        int pz = int(pos.z);
        int nx = px;
        int nz = pz;

        TR::Box &a = level->boxes[box];
            
        if (!checkPoint(px - radius, pz)) nx = a.minX + radius;
        if (!checkPoint(px + radius, pz)) nx = a.maxX - radius;
        if (!checkPoint(px, pz - radius)) nz = a.minZ + radius;
        if (!checkPoint(px, pz + radius)) nz = a.maxZ - radius;

        if (px != nx) pos.x = float(nx);
        if (pz != nz) pos.z = float(nz);
   }

    virtual void updatePosition() {
        if (!getEntity().flags.active) return;

        vec3 p = pos;
        pos += velocity * Core::deltaTime * 30.0f;

        clipByBox(pos);

        TR::Level::FloorInfo info;
        level->getFloorInfo(getRoomIndex(), (int)pos.x, (int)pos.y, (int)pos.z, info);

        if (info.boxIndex != 0xFFFF && zone == getZones()[info.boxIndex] && !level->boxes[info.boxIndex].overlap.block) {
            switch (stand) {
                case STAND_GROUND : {
                    float fallSpeed = 2048.0f * Core::deltaTime;
                    decrease(info.floor - pos.y, pos.y, fallSpeed);
                    break;
                }
                case STAND_AIR    : 
                    pos.y = clamp(pos.y, float(info.ceiling), float(info.floor));
                    break;
                default : ;
            }
        } else
            pos = p;

        updateEntity();
        checkRoom();
    }

    void setOverrides(bool active, int chest, int head) {
        if (active && head  > -1) {
            animation.overrides[head] = animation.getJointRot(head);
            animation.overrideMask |=  (1 << head);
        } else
            animation.overrideMask &= ~(1 << head);

        if (active && chest  > -1) {
            animation.overrides[chest] = animation.getJointRot(chest);
            animation.overrideMask |=  (1 << chest);
        } else
            animation.overrideMask &= ~(1 << chest);
    }

    void lookAt(int target, int chest, int head) {
        float speed = 8.0f * Core::deltaTime;
        quat rot;

        if (chest > -1) {
            if (aim(target, chest, vec4(-PI * 0.8f, PI * 0.8f, -PI * 0.75f, PI * 0.75f), rot))
                rotChest = rotChest.slerp(quat(0, 0, 0, 1).slerp(rot, 0.5f), speed);
            else 
                rotChest = rotChest.slerp(quat(0, 0, 0, 1), speed);
            animation.overrides[chest] = rotChest * animation.overrides[chest];
        }

        if (head > -1) {
            if (aim(target, head, vec4(-PI * 0.25f, PI * 0.25f, -PI * 0.5f, PI * 0.5f), rot))
                rotHead = rotHead.slerp(rot, speed);
            else
                rotHead = rotHead.slerp(quat(0, 0, 0, 1), speed);
            animation.overrides[head] = rotHead * animation.overrides[head];
        }
    }

    bool getTargetInfo(int height, vec3 *pos, float *angleX, float *angleY, float *dist) {
        vec3 p = waypoint;
        p.y -= height;
        if (pos) *pos = p;
        vec3 a = p - this->pos;
        if (dist) *dist = a.length();

        if (angleX || angleY) {
            a = a.normal();
            vec3 b = getDir();
            vec3 n = vec3(0, 1, 0);
            if (angleX) *angleX = 0.0f;
            if (angleY) *angleY = atan2f(b.cross(a).dot(n), a.dot(b));
        }
        return true;
    }

    int turn(float delta, float speed) {
        float w = speed * Core::deltaTime;

        updateTilt(delta, w, speed * 0.1f);

        if (delta != 0.0f) {
            decrease(delta, angle.y, w);
            if (speed != 0.0f) {
                velocity = velocity.rotateY(-w);
                return speed < 0 ? LEFT : RIGHT;
            }
        }
        return 0;
    }

    int lift(float delta, float speed) {
        speed *= Core::deltaTime;
        decrease(delta, pos.y, speed);
        if (speed != 0.0f) {
            updateEntity();
            return speed < 0 ? FORTH : BACK;
        }
        return 0;
    }


    virtual void hit(int damage, Controller *enemy = NULL) {
        Character::hit(damage, enemy);
        wound = true;
    };

    void bite(const vec3 &pos, int damage) {
        ASSERT(target);
        target->hit(damage, this);
        Sprite::add(game, TR::Entity::BLOOD, target->getRoomIndex(), (int)pos.x, (int)pos.y, (int)pos.z, Sprite::FRAME_ANIMATED);
    }

    #define STALK_BOX       (1024 * 3)
    #define ESCAPE_BOX      (1024 * 5)
    #define ATTACK_BOX      STALK_BOX
    
    Mood getMoodFixed() {
        bool inZone = zone == target->zone;

        if (mood == MOOD_SLEEP || mood == MOOD_STALK)
            return inZone ? MOOD_ATTACK : (wound ? MOOD_ESCAPE : mood);
        
        if (mood == MOOD_ATTACK)
            return inZone ? mood : MOOD_SLEEP;

        return inZone ? MOOD_ATTACK : mood;
    }

    Mood getMoodRandom() {
        bool inZone = zone == target->zone;
        bool brave  = rand() < (mood != MOOD_ESCAPE ? 0x7800 : 0x0100) && inZone;
            
        if (mood == MOOD_SLEEP || mood == MOOD_STALK) {
            if (wound && !brave)
                return MOOD_ESCAPE;
            if (inZone) {
                int dx = abs(int(pos.x - target->pos.x));
                int dz = abs(int(pos.z - target->pos.z));
                return ((dx <= ATTACK_BOX && dz <= ATTACK_BOX) || (mood == MOOD_STALK && targetBox == -1)) ? MOOD_ATTACK : MOOD_STALK;
            }
            return mood;
        }

        if (mood == MOOD_ATTACK)
            return (wound && !brave) ? MOOD_ESCAPE : (!inZone ? MOOD_SLEEP : mood);

        return brave ? MOOD_STALK : mood;
    }
    
    bool think(bool fixedLogic) {
        thinkTime += Core::deltaTime;
        if (thinkTime < 1.0f / 30.0f)
            return false;
        thinkTime -= 1.0f / 30.0f;

        if (!target) {
            mood = MOOD_SLEEP;
            return true;
        }

        int targetBoxOld = targetBox;

    // update mood
        bool inZone = zone == target->zone;

        if (mood != MOOD_ATTACK && targetBox > -1 && !checkBox(targetBox)) {
            if (!inZone)
                mood = MOOD_SLEEP;
            targetBox = -1;
        }

        mood = target->health <= 0 ? MOOD_SLEEP : (ai == AI_FIXED ? getMoodFixed() : getMoodRandom());

    // set behavior and target
        int box;

        switch (mood) {
            case MOOD_SLEEP :
                if (targetBox == -1 && checkBox(box = getRandomZoneBox()) && isStalkBox(box)) {
                    mood = MOOD_STALK;
                    gotoBox(box);
                }
                break;
            case MOOD_STALK :
                if ((targetBox == -1 || !isStalkBox(targetBox)) && checkBox(box = getRandomZoneBox())) {
                    if (isStalkBox(box))
                        gotoBox(box);
                    else
                        if (targetBox == -1) {
                            if (!inZone)
                                mood = MOOD_SLEEP;
                            gotoBox(box);
                        }
                }
                break;
            case MOOD_ATTACK :
                if (randf() > aggression)
                    break;
                targetBox = -1;
                break;
            case MOOD_ESCAPE :
                if (targetBox == -1 && checkBox(box = getRandomZoneBox())) {
                    if (isEscapeBox(box))
                        gotoBox(box);
                    else
                        if (inZone && isStalkBox(box)) {
                            mood = MOOD_STALK;
                            gotoBox(box);
                        }                    
                }
                break;
        }

        if (targetBox == -1)
            gotoBox(target->box);

        if (path && this->box != path->boxes[path->index - 1] && this->box != path->boxes[path->index])
            targetBoxOld = -1;

        if (targetBoxOld != targetBox) {
            if (findPath(stepHeight, dropHeight))
                nextWaypoint();
            else
                targetBox = -1;
        }

        if (targetBox != -1 && path) {
            vec3 d = pos - waypoint;

            if (fabsf(d.x) < 512 && fabsf(d.y) < 512 && fabsf(d.z) < 512)
                nextWaypoint();
        }

        return true;
    }

    void nextWaypoint() {
        if (!path->getNextPoint(level, waypoint))
            waypoint = target->pos;
        if (flying) {
            if (target->stand != STAND_ONWATER)
                waypoint.y -= 765.0f;
            else
                waypoint.y -= 64.0f;
        }
    }

    uint16 getRandomZoneBox() {
        return game->getRandomBox(zone, getZones());
    }

    void gotoBox(int box) {
        targetBox = box;
    }

    bool checkBox(int box) {
        if (zone != getZones()[box])
            return false;

        TR::Entity &e = getEntity();
        TR::Box    &b = game->getLevel()->boxes[box];
        TR::Entity::Type type = e.type;

        if (b.overlap.block)
            return false;

        if (type == TR::Entity::ENEMY_REX || type == TR::Entity::ENEMY_MUTANT_1 || type == TR::Entity::ENEMY_CENTAUR) {
            if (b.overlap.blockable)
                return false;
        } else
            if (b.overlap.block)
                return false;

        return e.x < int(b.minX) || e.x > int(b.maxX) || e.z < int(b.minZ) || e.z > int(b.maxZ);
    }

    bool isStalkBox(int box) {
        TR::Entity &t = target->getEntity();
        TR::Box    &b = game->getLevel()->boxes[box];

        int x = (b.minX + b.maxX) / 2 - t.x;
        if (abs(x) > STALK_BOX) return false;

        int z = (b.minZ + b.maxZ) / 2 - t.z;
        if (abs(z) > STALK_BOX) return false;

        // TODO: check for some quadrant shit

        return true;
    }

    bool isEscapeBox(int box) {
        TR::Entity &e = getEntity();
        TR::Entity &t = target->getEntity();
        TR::Box    &b = game->getLevel()->boxes[box];

        int x = (b.minX + b.maxX) / 2 - t.x;
        if (abs(x) < ESCAPE_BOX) return false;

        int z = (b.minZ + b.maxZ) / 2 - t.z;
        if (abs(z) < ESCAPE_BOX) return false;

        return !((e.x > t.x) ^ (x > 0)) || !((e.z > t.z) ^ (z > 0));
    }

    bool findPath(int ascend, int descend) {
        delete path;
        path = NULL;

        uint16 *boxes;
        uint16 count = game->findPath(ascend, descend, box, targetBox, getZones(), &boxes);
        if (count) {
            path = new Path(level, boxes, count);
            return true;
        }

        return false;
    }
};

#define WOLF_TURN_FAST   (DEG2RAD * 150)
#define WOLF_TURN_SLOW   (DEG2RAD * 60)

#define WOLF_DIST_STALK  STALK_BOX
#define WOLF_DIST_BITE   345
#define WOLF_DIST_ATTACK (1024 + 512)


struct Wolf : Enemy {

    enum {
        ANIM_DEATH      = 20,
        ANIM_DEATH_RUN  = 21,
        ANIM_DEATH_JUMP = 22,
    };

    enum {
        STATE_NONE   ,
        STATE_STOP   ,
        STATE_WALK   ,
        STATE_RUN    ,
        STATE_JUMP   , // unused
        STATE_STALK  ,
        STATE_ATTACK ,
        STATE_HOWL   ,
        STATE_SLEEP  ,
        STATE_GROWL  ,
        STATE_TURN   , // unused
        STATE_DEATH  ,
        STATE_BITE   ,
    };

    Wolf(IGame *game, int entity) : Enemy(game, entity, 6, 341, 0.25f) {
        dropHeight = -1024;
        jointChest = 2;
        jointHead  = 3;
        nextState  = STATE_NONE;
    }

    virtual int getStateGround() {
        TR::Entity &e = getEntity();
        if (!e.flags.active)
            return (state == STATE_STOP || state == STATE_SLEEP) ? STATE_SLEEP : STATE_STOP;

        if (!think(false))
            return state;

        float angle, dist;
        getTargetInfo(0, NULL, NULL, &angle, NULL);

        dist = (target && target->health > 0) ? (pos - target->pos).length() : +INF;
        
        bool inZone = target ? target->zone == zone : false;

        if (nextState == state)
            nextState = STATE_NONE;

        switch (state) {
            case STATE_SLEEP    :
                if (mood == MOOD_ESCAPE || inZone) {
                    nextState = STATE_GROWL;
                    return STATE_STOP;
                }
                if (randf() < 0.0001f) {
                    nextState = STATE_WALK;
                    return STATE_STOP;
                }
                break;
            case STATE_STOP     : return nextState != STATE_NONE ? nextState : STATE_WALK;
            case STATE_WALK     : 
                if (mood != MOOD_SLEEP) {
                    nextState = STATE_NONE;
                    return STATE_STALK;
                }
                if (randf() < 0.0001f) {
                    nextState = STATE_SLEEP;
                    return STATE_STOP;
                }
                break;
            case STATE_GROWL    :
                if (nextState != STATE_NONE) return nextState;
                if (mood == MOOD_ESCAPE)     return STATE_RUN;
                if (dist < WOLF_DIST_BITE)   return STATE_BITE;
                if (mood == MOOD_STALK)      return STATE_STALK;
                if (mood == MOOD_SLEEP)      return STATE_STOP;
                return STATE_RUN;
            case STATE_STALK    : 
                if (mood == MOOD_ESCAPE)    return STATE_RUN;
                if (dist < WOLF_DIST_BITE)  return STATE_BITE;
                if (dist > WOLF_DIST_STALK) return STATE_RUN;
                if (mood == MOOD_ATTACK)    return STATE_RUN;
                if (randf() < 0.012f) {
                    nextState = STATE_HOWL;
                    return STATE_GROWL;
                }
                if (mood == MOOD_SLEEP)     return STATE_GROWL;
                break;
            case STATE_RUN      :
                if (dist < WOLF_DIST_ATTACK) {
                    if (dist < WOLF_DIST_ATTACK * 0.5f && fabsf(angle) < PI * 0.5f) {
                        nextState = STATE_NONE;
                        return STATE_ATTACK;
                    }
                    nextState = STATE_STALK;
                    return STATE_GROWL;
                }
                if (mood == MOOD_STALK && dist < WOLF_DIST_STALK) {
                    nextState = STATE_STALK;
                    return STATE_GROWL;
                }
                if (mood == MOOD_SLEEP) return STATE_GROWL;
                break;
            case STATE_ATTACK :
            case STATE_BITE   :
                if (nextState == STATE_NONE && target->health > 0 && collide(target)) {
                    bite(animation.getJoints(getMatrix(), jointHead, true).pos, state == STATE_ATTACK ? 50 : 100);
                    nextState = state == STATE_ATTACK ? STATE_RUN : STATE_GROWL;
                }
                return state == STATE_ATTACK ? STATE_RUN : state;
        }

        return state;
    }

    virtual int getStateDeath() {
        switch (state) {
            case STATE_DEATH : return state;
            case STATE_RUN   : return animation.setAnim(ANIM_DEATH_RUN);
            case STATE_JUMP  : return animation.setAnim(ANIM_DEATH_JUMP);
            default          : return animation.setAnim(ANIM_DEATH);
        }
    }

    virtual void updatePosition() {
        float angleY = 0.0f;
        if (state == STATE_RUN || state == STATE_WALK || state == STATE_STALK)
            getTargetInfo(0, NULL, NULL, &angleY, NULL);

        turn(angleY, state == STATE_RUN ? WOLF_TURN_FAST : WOLF_TURN_SLOW);

        if (state == STATE_DEATH) {
            animation.overrideMask = 0;
            return;
        }

        Enemy::updatePosition();
        setOverrides(state == STATE_RUN || state == STATE_WALK || state == STATE_STALK, jointChest, jointHead);
        lookAt(target ? target->entity : -1, jointChest, jointHead);
    }
};

#define BEAR_DIST_EAT    768
#define BEAR_DIST_HOWL   2048
#define BEAR_DIST_BITE   1024
#define BEAR_DIST_ATTACK 600

#define BEAR_TURN_FAST   (DEG2RAD * 150)
#define BEAR_TURN_SLOW   (DEG2RAD * 60)

struct Bear : Enemy {

    enum {
        ANIM_DEATH_HIND = 19,
        ANIM_DEATH      = 20,
    };

    enum {
        STATE_NONE   = -1,
        STATE_WALK   ,
        STATE_STOP   ,
        STATE_HIND   ,
        STATE_RUN    ,
        STATE_HOWL   ,
        STATE_GROWL  ,
        STATE_BITE   ,
        STATE_ATTACK ,
        STATE_EAT    ,
        STATE_DEATH  ,
    };

    Bear(IGame *game, int entity) : Enemy(game, entity, 20, 341, 0.5f) {
        jointChest = 13;
        jointHead  = 14;    
        nextState  = STATE_NONE;
    }

    virtual int getStateGround() {
        if (!getEntity().flags.active)
            return state;

        if (!think(false))
            return state;

        if (nextState == state)
            nextState = STATE_NONE;

        float dist = target ? (pos - target->pos).length() : +INF;

        bool targetDead = target->health <= 0;

        switch (state) {
            case STATE_WALK     :
                if (targetDead && collide(target))
                    return STATE_STOP; // eat lara! >:E
                else
                    if (mood != MOOD_SLEEP) {
                        if (mood == MOOD_ESCAPE)
                            nextState = STATE_NONE;
                        return STATE_STOP;
                    } else if (randf() < 0.003f) {
                        nextState = STATE_GROWL;
                        return STATE_STOP;
                    }
                break;
            case STATE_STOP     :
                if (targetDead)
                    return dist <= BEAR_DIST_EAT ? STATE_EAT : STATE_WALK;
                else
                    return nextState != STATE_NONE ? nextState : (mood == MOOD_SLEEP ? STATE_WALK : STATE_RUN);
            case STATE_HIND     :
                if (collide(target)) {
                    return STATE_HOWL;
                }
                if (mood == MOOD_ESCAPE) {
                    nextState = STATE_NONE;
                    return STATE_HOWL;
                }
                if (mood == MOOD_SLEEP || randf() < 0.003f) {
                    nextState = STATE_GROWL;
                    return STATE_HOWL;
                }
                if (dist > BEAR_DIST_HOWL || randf() < 0.05f) {
                    nextState = STATE_STOP;
                    return STATE_HOWL;
                }
                break;
            case STATE_RUN      :
                if (collide(target))
                    target->hit(3, this);                
                if (targetDead || mood == MOOD_SLEEP)
                    return STATE_STOP;
                if (dist < BEAR_DIST_HOWL && randf() < 0.025f) {
                    nextState = STATE_HOWL;
                    return STATE_STOP;
                } else if (dist < BEAR_DIST_BITE) {
                    nextState = STATE_NONE;
                    return STATE_BITE;
                }
                break;
            case STATE_HOWL     :
                if (nextState != STATE_NONE) return nextState;
                if (mood == MOOD_SLEEP || mood == MOOD_ESCAPE) return STATE_STOP;
                if (dist < BEAR_DIST_ATTACK) return STATE_ATTACK;
                return STATE_HIND;
            case STATE_BITE     :
            case STATE_ATTACK   :
                if (nextState == STATE_NONE && collide(target)) {
                    bite(animation.getJoints(getMatrix(), jointHead, true).pos, state == STATE_BITE ? 200 : 400);
                    nextState = state == STATE_BITE ? STATE_STOP : STATE_HOWL;
                }
                break;
        }

        return state;
    }

    virtual int getStateDeath() {
        switch (state) {
            case STATE_HIND : return STATE_HOWL;
            case STATE_RUN  :
            case STATE_WALK : return STATE_STOP;
            case STATE_HOWL :
            case STATE_STOP : return STATE_DEATH;
        }
        return state;// == STATE_DEATH ? state : animation.setAnim(ANIM_DEATH);
    }

    virtual void updatePosition() {
        float angleY = 0.0f;
        if (state == STATE_RUN || state == STATE_WALK || state == STATE_HIND)
            getTargetInfo(0, NULL, NULL, &angleY, NULL);

        turn(angleY, state == STATE_RUN ? BEAR_TURN_FAST : BEAR_TURN_SLOW);

        if (state == STATE_DEATH) {
            animation.overrideMask = 0;
            return;
        }

        Enemy::updatePosition();
        setOverrides(state == STATE_RUN || state == STATE_WALK || state == STATE_HIND, jointChest, jointHead);
        lookAt(target ? target->entity : -1, jointChest, jointHead);
    }
};


#define BAT_TURN_SPEED  (DEG2RAD * 300)
#define BAT_LIFT_SPEED  512.0f

struct Bat : Enemy {

    enum {
        ANIM_DEATH = 4,
    };

    enum {
        STATE_NONE,
        STATE_AWAKE,
        STATE_FLY,
        STATE_ATTACK,
        STATE_CIRCLING,
        STATE_DEATH,
    };

    Bat(IGame *game, int entity) : Enemy(game, entity, 1, 102, 0.03f) {
        stand = STAND_AIR;
        stepHeight =  20 * 1024;
        dropHeight = -20 * 1024;
        jointHead  = 4;
    }

    virtual int getStateAir() {
        if (!getEntity().flags.active) {
            animation.time = 0.0f;
            animation.dir  = 0.0f;
            return STATE_AWAKE;
        }

        if (!think(false))
            return state;

        switch (state) {
            case STATE_AWAKE  : return STATE_FLY;
            case STATE_ATTACK : 
                if (!collide(target)) {
                    mood = MOOD_SLEEP;
                    return STATE_FLY;
                } else
                    bite(animation.getJoints(getMatrix(), jointHead, true).pos, 2);
                break;
            case STATE_FLY    : 
                if (collide(target)) {
                    mood = MOOD_ATTACK;
                    return STATE_ATTACK;
                }
                break;
        }

        return state;
    }

    virtual int getStateDeath() {
        return state == STATE_DEATH ? state : animation.setAnim(ANIM_DEATH);
    }

    virtual void updateVelocity() {
        if (state != STATE_DEATH)
            Enemy::updateVelocity();
        else
            velocity = vec3(0.0f, velocity.y + GRAVITY * Core::deltaTime, 0.0f);
    }

    virtual void updatePosition() {
        float angleY = 0.0f;
        if (state == STATE_FLY || state == STATE_ATTACK)
            getTargetInfo(0, NULL, NULL, &angleY, NULL);
        turn(angleY, BAT_TURN_SPEED);
        if (flying)
            lift(waypoint.y - pos.y, BAT_LIFT_SPEED);
        Enemy::updatePosition();
    }
};

#endif
