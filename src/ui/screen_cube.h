#pragma once
#include "screen.h"

class ScreenCube : public Screen {
public:
    ~ScreenCube();

    void        init(uint16_t contentW, uint16_t contentH) override;
    void        update(lgfx::LovyanGFX& gfx,
                       uint16_t contentW, uint16_t contentH) override;
    void        onTouch(uint16_t x, uint16_t y) override;
    const char* name() const override { return "Cube Demo"; }

private:
    static constexpr int SPRITE_W = 300;
    static constexpr int SPRITE_H = 300;

    lgfx::LGFX_Sprite* _canvas = nullptr;

    int   _spriteX = 0, _spriteY = 0;  // sprite top-left in content area
    float _posX    = 150.0f;            // cube centre in sprite-local coords
    float _posY    = 150.0f;
    float _angX    = 0.4f;
    float _angY    = 0.0f;
};
