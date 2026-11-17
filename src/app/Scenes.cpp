#include "app/Scenes.h"

#include <array>

#include "app/FrontEnd.h"
#include "app/Menu.h"

namespace reaction {

namespace {

// The 16 EGA colours (used until a scene has its game palette).
Palette egaPalette() {
    static constexpr std::array<Rgb, 16> ega = {{
        {0, 0, 0}, {0, 0, 170}, {0, 170, 0}, {0, 170, 170}, {170, 0, 0}, {170, 0, 170},
        {170, 85, 0}, {170, 170, 170}, {85, 85, 85}, {85, 85, 255}, {85, 255, 85},
        {85, 255, 255}, {255, 85, 85}, {255, 85, 255}, {255, 255, 85}, {255, 255, 255},
    }};
    Palette p;
    for (std::size_t i = 0; i < 16; ++i) p.colors[i] = ega[i];
    return p;
}

// A menu over a plain background: exercises fonts, text windows and input.
class MenuDemoScene : public Scene {
public:
    explicit MenuDemoScene(App& app)
        : app_(app),
          menu_(app.fonts.font(1), "Covert Reaction\n New Game\n Load Game\n Credits\n Quit\n", 104, 70) {}

    void handle(const SDL_Event& e) override { menu_.handle(e); }
    void tick() override {
        menu_.tick();
        if (auto r = menu_.result()) {
            if (*r == 3 || *r < 0) quit = true;
            else menu_ = Menu(app_.fonts.font(1), "Covert Reaction\n New Game\n Load Game\n Credits\n Quit\n", 104, 70);
        }
    }
    void draw(Frame& f) override {
        f.palette = egaPalette();
        f.image.fill(1);
        menu_.draw(f.image);
    }

private:
    App& app_;
    Menu menu_;
};

}  // namespace

std::map<std::string, SceneFactory>& sceneRegistry() {
    static std::map<std::string, SceneFactory> r;
    return r;
}

std::unique_ptr<Scene> makeScene(App& app, const std::string& name) {
    auto& r = sceneRegistry();
    auto it = r.find(name == "start" ? std::string("title") : name);
    return it == r.end() ? nullptr : it->second(app);
}

REACTION_SCENE("menu-demo", [](App& app) { return std::make_unique<MenuDemoScene>(app); });

}  // namespace reaction
