#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <algorithm>
#include <vector>
using namespace geode::prelude;

namespace {
    struct LevelCache {
        std::vector<CCRect> hazards;
        std::vector<CCRect> solids;
        float maxW = 0.f;
    };
    LevelCache g_level;

    enum class Mode { Cube, Ship, Ball, Ufo, Wave, Robot, Spider, Swing };

    Mode modeOf(PlayerObject* p) {
        if (p->m_isShip) return Mode::Ship;
        if (p->m_isBird) return Mode::Ufo;
        if (p->m_isBall) return Mode::Ball;
        if (p->m_isDart) return Mode::Wave;
        if (p->m_isRobot) return Mode::Robot;
        if (p->m_isSpider) return Mode::Spider;
        if (p->m_isSwing) return Mode::Swing;
        return Mode::Cube;
    }

    void sortByX(std::vector<CCRect>& v) {
        std::sort(v.begin(), v.end(), [](CCRect const& a, CCRect const& b) {
            return a.origin.x < b.origin.x;
        });
    }

    auto firstNear(std::vector<CCRect> const& v, float x) {
        float minX = x - g_level.maxW;
        return std::lower_bound(v.begin(), v.end(), minX, [](CCRect const& r, float val) {
            return r.origin.x < val;
        });
    }

    bool hits(std::vector<CCRect> const& v, CCRect const& box) {
        float endX = box.origin.x + box.size.width;
        for (auto it = firstNear(v, box.origin.x); it != v.end() && it->origin.x <= endX; ++it) {
            if (it->intersectsRect(box)) return true;
        }
        return false;
    }

    // ¿Hay algo peligroso si el jugador está a altura y entre x0 y x1?
    bool danger(float y, float x0, float x1) {
        CCRect box(x0, y - 11.f, x1 - x0, 22.f);
        return hits(g_level.hazards, box) || hits(g_level.solids, box);
    }

    // A qué altura caería el jugador al cambiar de superficie (araña/bola).
    bool findDest(bool upsideDown, float x, float y, float& out) {
        float lo = x - 10.f, hi = x + 10.f;
        bool found = false;
        float best = 0.f;
        for (auto it = firstNear(g_level.solids, lo); it != g_level.solids.end() && it->origin.x <= hi; ++it) {
            if (it->origin.x + it->size.width < lo) continue;
            float bottom = it->origin.y;
            float top = it->origin.y + it->size.height;
            if (!upsideDown) {
                if (bottom >= y + 14.f && (!found || bottom < best)) { best = bottom; found = true; }
            } else {
                if (top <= y - 14.f && (!found || top > best)) { best = top; found = true; }
            }
        }
        if (!found) return false;
        out = upsideDown ? best + 15.f : best - 15.f;
        return true;
    }

    // Altura ideal en el centro del hueco libre más cercano (nave, UFO, wave, swing).
    bool targetY(float y, float x0, float x1, float& target) {
        float bottom = 0.f, top = 0.f;
        bool hasB = false, hasT = false;
        auto scan = [&](std::vector<CCRect> const& v) {
            for (auto it = firstNear(v, x0); it != v.end() && it->origin.x <= x1; ++it) {
                if (it->origin.x + it->size.width < x0) continue;
                float low = it->origin.y;
                float high = it->origin.y + it->size.height;
                if ((low + high) * 0.5f > y) {
                    if (!hasT || low < top) { top = low; hasT = true; }
                } else {
                    if (!hasB || high > bottom) { bottom = high; hasB = true; }
                }
            }
        };
        scan(g_level.solids);
        scan(g_level.hazards);
        if (!hasB && !hasT) return false;
        if (!hasT) top = bottom + 120.f;
        if (!hasB) bottom = top - 120.f;
        target = ((bottom + 15.f) + (top - 15.f)) * 0.5f;
        return true;
    }
}

class $modify(ProBotLayer, PlayLayer) {
    struct Fields {
        float lastX = 0.f;
        float lastY = 0.f;
        float cooldown = 0.f;
        float holdTime = 0.f;
        bool down = false;
        int lastMode = -1;
    };

    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();
        g_level = LevelCache();
        for (auto obj : CCArrayExt<GameObject*>(m_objects)) {
            if (!obj) continue;
            auto type = obj->m_objectType;
            if (type != GameObjectType::Hazard && type != GameObjectType::Solid) continue;
            CCRect r = obj->getObjectRect();
            g_level.maxW = std::max(g_level.maxW, r.size.width);
            if (type == GameObjectType::Hazard) g_level.hazards.push_back(r);
            else g_level.solids.push_back(r);
        }
        sortByX(g_level.hazards);
        sortByX(g_level.solids);
    }

    void setDown(PlayerObject* p, bool down) {
        if (m_fields->down == down) return;
        m_fields->down = down;
        if (down) p->pushButton(PlayerButton::Jump);
        else p->releaseButton(PlayerButton::Jump);
    }

    void tap(PlayerObject* p, float hold, float cd) {
        setDown(p, true);
        m_fields->holdTime = hold;
        m_fields->cooldown = cd;
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        auto p = m_player1;
        if (!p) return;

        if (!Mod::get()->getSettingValue<bool>("enabled") || p->m_isDead || dt <= 0.f) {
            setDown(p, false);
            return;
        }

        float x = p->getPositionX();
        float y = p->getPositionY();
        float speed = (x - m_fields->lastX) / dt;
        float vy = (y - m_fields->lastY) / dt;
        m_fields->lastX = x;
        m_fields->lastY = y;

        Mode mode = modeOf(p);
        if (static_cast<int>(mode) != m_fields->lastMode) {
            setDown(p, false);
            m_fields->lastMode = static_cast<int>(mode);
            m_fields->holdTime = 0.f;
        }

        // Suelta el botón cuando termina un toque.
        if (m_fields->holdTime > 0.f) {
            m_fields->holdTime -= dt;
            if (m_fields->holdTime <= 0.f) setDown(p, false);
        }
        if (m_fields->cooldown > 0.f) m_fields->cooldown -= dt;
        bool ready = m_fields->cooldown <= 0.f;

        if (speed < 1.f) return;

        float ms = static_cast<float>(Mod::get()->getSettingValue<int64_t>("lookahead-ms"));
        float la = std::max(speed * ms / 1000.f, 25.f);
        float x0 = x - 5.f;
        float x1 = x + 15.f + la;
        float wide = std::max(speed * ms * 2.f / 1000.f, 60.f);
        float xw = x + 15.f + wide;

        switch (mode) {
            case Mode::Cube:
            case Mode::Robot: {
                if (!ready || !p->m_isOnGround) return;
                if (danger(y, x0, x1)) {
                    tap(p, mode == Mode::Robot ? 0.2f : 0.001f, 0.15f);
                }
                return;
            }
            case Mode::Spider:
            case Mode::Ball: {
                if (!ready) return;
                if (!danger(y, x0, x1)) return;
                float destY = 0.f;
                if (!findDest(p->m_isUpsideDown, x, y, destY)) return;
                if (danger(destY, x0, x1)) return;
                tap(p, 0.001f, 0.1f);
                return;
            }
            case Mode::Ship: {
                float target = 0.f;
                if (!targetY(y, x - 5.f, xw, target)) return;
                float pred = y + vy * 0.12f;
                if (pred < target - 4.f) setDown(p, true);
                else if (pred > target + 4.f) setDown(p, false);
                return;
            }
            case Mode::Wave: {
                float target = 0.f;
                if (!targetY(y, x - 5.f, xw, target)) return;
                if (y < target - 2.f) setDown(p, true);
                else if (y > target + 2.f) setDown(p, false);
                return;
            }
            case Mode::Ufo: {
                if (!ready) return;
                float target = 0.f;
                if (!targetY(y, x - 5.f, xw, target)) return;
                float pred = y + vy * 0.12f;
                if (pred < target - 4.f) tap(p, 0.001f, 0.12f);
                return;
            }
            case Mode::Swing: {
                if (!ready) return;
                float target = 0.f;
                if (!targetY(y, x - 5.f, xw, target)) return;
                float pred = y + vy * 0.12f;
                if (p->m_isUpsideDown ? pred > target : pred < target) {
                    tap(p, 0.001f, 0.12f);
                }
                return;
            }
        }
    }
};
