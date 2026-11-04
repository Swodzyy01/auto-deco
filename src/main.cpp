#include <Geode/Geode.hpp>
#include <Geode/modify/EditorUI.hpp>

#include <random>
#include <string>
#include <vector>

using namespace geode::prelude;

// ---------------------------------------------------------------
//  TEMALAR
//  Her tema: isim, ayar anahtarı, 2 renk (A/B), renk kanalı başlangıcı
//  Kanallar: 970-979 arası kullanılır (tema başına 2 kanal)
// ---------------------------------------------------------------
struct ThemeDef {
    const char* name;
    const char* settingKey;   // mod.json'daki ID listesi ayarı
    ccColor3B colorA;
    ccColor3B colorB;
    int channelBase;
};

static const ThemeDef THEMES[5] = {
    {"Aqua",         "aqua-ids",   {0, 220, 255},   {0, 110, 255},  970},
    {"Hell",         "hell-ids",   {255, 40, 0},    {140, 0, 0},    972},
    {"Heaven",       "heaven-ids", {255, 255, 255}, {255, 240, 170}, 974},
    {"Yellow Light", "yellow-ids", {255, 235, 50},  {255, 170, 0},  976},
    {"Green Bullet", "green-ids",  {60, 255, 60},   {0, 150, 40},   978},
};

static std::vector<int> parseIds(std::string const& s) {
    std::vector<int> out;
    std::string cur;
    auto flush = [&]() {
        if (cur.empty()) return;
        try { out.push_back(std::stoi(cur)); } catch (...) {}
        cur.clear();
    };
    for (char c : s) {
        if (c == ',' || c == ' ' || c == ';') flush();
        else cur.push_back(c);
    }
    flush();
    return out;
}

// Temaya göre dekorun kaynak objeye göre nereye düşeceği
struct Placement {
    float dx, dy, rot;
};

static Placement makePlacement(int theme, std::mt19937& rng) {
    auto r = [&](float a, float b) {
        return std::uniform_real_distribution<float>(a, b)(rng);
    };
    switch (theme) {
        case 0: // Aqua: yukarı yükselen baloncuklar
            return {r(-45.f, 45.f), r(-15.f, 75.f), r(0.f, 360.f)};
        case 1: // Hell: aşağı sarkan sivri şeyler
            return {r(-30.f, 30.f), r(-45.f, 15.f), 180.f + r(-20.f, 20.f)};
        case 2: // Heaven: üstte bulut gibi dağınık
            return {r(-60.f, 60.f), r(30.f, 105.f), r(-10.f, 10.f)};
        case 3: // Yellow Light: etrafa ışın gibi 45 derecelik açılar
            return {r(-30.f, 30.f), r(-30.f, 30.f),
                    45.f * (float)std::uniform_int_distribution<int>(0, 7)(rng)};
        default: // Green Bullet: arkaya doğru yatay mermi izi
            return {r(-90.f, 0.f), r(-15.f, 15.f), 90.f};
    }
}

class $modify(AutoDecoEditorUI, EditorUI) {
    struct Fields {
        int theme = 0;
        ButtonSprite* themeLabel = nullptr;
    };

    bool init(LevelEditorLayer* lel) {
        if (!EditorUI::init(lel)) return false;

        auto winSize = CCDirector::get()->getWinSize();

        auto menu = CCMenu::create();
        menu->setID("auto-deco-menu"_spr);
        menu->setPosition({75.f, winSize.height - 75.f});
        menu->setScale(0.7f);

        m_fields->themeLabel = ButtonSprite::create(THEMES[0].name);
        auto themeBtn = CCMenuItemSpriteExtra::create(
            m_fields->themeLabel, this,
            menu_selector(AutoDecoEditorUI::onCycleTheme)
        );
        themeBtn->setPosition({0.f, 0.f});

        auto applyBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Auto Deco"), this,
            menu_selector(AutoDecoEditorUI::onApplyDeco)
        );
        applyBtn->setPosition({0.f, -38.f});

        menu->addChild(themeBtn);
        menu->addChild(applyBtn);
        this->addChild(menu, 100);

        return true;
    }

    void onCycleTheme(CCObject*) {
        m_fields->theme = (m_fields->theme + 1) % 5;
        if (m_fields->themeLabel) {
            m_fields->themeLabel->setString(THEMES[m_fields->theme].name);
        }
    }

    void addColorTrigger(int channel, ccColor3B color, CCPoint pos) {
        auto obj = m_editorLayer->createObject(899, pos, false); // 899 = Color trigger
        if (!obj) return;
        if (auto t = typeinfo_cast<EffectGameObject*>(obj)) {
            t->m_targetColorID = channel;
            t->m_triggerTargetColor = color;
            t->m_duration = 0.f;
        }
    }

    void onApplyDeco(CCObject*) {
        auto const& theme = THEMES[m_fields->theme];

        auto ids = parseIds(Mod::get()->getSettingValue<std::string>(theme.settingKey));
        if (ids.empty()) {
            FLAlertLayer::create("Auto Deco", "Bu tema icin obje ID listesi bos (mod ayarlarindan doldur).", "OK")->show();
            return;
        }

        int perObject = (int)Mod::get()->getSettingValue<int64_t>("per-object");
        int maxSources = (int)Mod::get()->getSettingValue<int64_t>("max-sources");

        // Kaynak objeler: seçim varsa seçim, yoksa tüm level
        std::vector<GameObject*> sources;
        auto selected = this->getSelectedObjects();
        if (selected && selected->count() > 0) {
            for (auto obj : CCArrayExt<GameObject*>(selected)) sources.push_back(obj);
        } else if (m_editorLayer->m_objects) {
            for (auto obj : CCArrayExt<GameObject*>(m_editorLayer->m_objects)) sources.push_back(obj);
        }

        std::mt19937 rng(std::random_device{}());
        int created = 0;
        int used = 0;

        for (auto src : sources) {
            if (used >= maxSources) break;
            if (!src) continue;
            if (src->m_isNoTouch) continue;         // onceki dekorlari tekrar dekorlama
            if (src->m_objectID >= 899) continue;   // trigger vb. atla
            ++used;

            auto base = src->getPosition();
            for (int i = 0; i < perObject; i++) {
                auto pl = makePlacement(m_fields->theme, rng);
                int id = ids[std::uniform_int_distribution<size_t>(0, ids.size() - 1)(rng)];

                auto obj = m_editorLayer->createObject(id, base + CCPoint{pl.dx, pl.dy}, false);
                if (!obj) continue;

                obj->m_isNoTouch = true;  // dekor oyunu bozmasin (hasar/carpisma yok)
                obj->setRotation(pl.rot);

                float sc = std::uniform_real_distribution<float>(0.3f, 0.9f)(rng);
                obj->updateCustomScaleX(sc);
                obj->updateCustomScaleY(sc);

                int ch = theme.channelBase + (int)(rng() % 2);
                if (obj->m_baseColor)   obj->m_baseColor->m_colorID = ch;
                if (obj->m_detailColor) obj->m_detailColor->m_colorID = ch;
                ++created;
            }
        }

        // Renk kanallarini ayarlayan color trigger'lar (level basinda)
        if (created > 0) {
            addColorTrigger(theme.channelBase,     theme.colorA, {-30.f, 300.f});
            addColorTrigger(theme.channelBase + 1, theme.colorB, {-30.f, 330.f});
        }

        FLAlertLayer::create(
            "Auto Deco",
            fmt::format("{} tema: {} dekor eklendi.", theme.name, created),
            "OK"
        )->show();
    }
};
