#include "emotion_display.h"
#include "lvgl_theme.h"

#include <esp_log.h>
#include <font_awesome.h>

#include <algorithm>
#include <cstring>
#include <string>

#include "display/oled_display.h"

#define TAG "EmotionDisplay"

// 表情映射表 - 将表情名称映射到对应的GIF
const EmotionDisplay::EmotionMap EmotionDisplay::emotion_maps_[] = {

    // 中性/平静类表情
    {"neutral", &blink_once_gif},
    {"relaxed", &blink_twice_gif},
    {"sleepy", &look_left_gif},

    // 积极/开心类表情
    {"happy", &excited_gif},
    {"laughing", &excited_gif},
    {"funny", &excited_gif},
    {"loving", &excited_gif},
    {"confident", &excited_gif},
    {"winking", &excited_gif},
    {"cool", &excited_gif},
    {"delicious", &excited_gif},
    {"kissy", &excited_gif},
    {"silly", &excited_gif},

    // 悲伤类表情
    {"sad", &sad_gif},
    {"crying", &sad_gif},

    // 愤怒类表情
    {"angry", &anger_gif},

    // 惊讶类表情
    {"surprised", &excited_gif},
    {"shocked", &excited_gif},

    // 思考/困惑类表情
    {"thinking", &look_right_gif},
    {"confused", &disdain_gif},
    {"embarrassed", &fear_gif},

    {nullptr, nullptr} // 结束标记
};

EmotionDisplay::EmotionDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                               int width, int height, bool mirror_x, bool mirror_y)
    : OledDisplay(panel_io, panel, width, height, mirror_x, mirror_y),
      emotion_gif_(nullptr), emotion_content_(nullptr)
{
    HideUnnecessaryElements();
    SetupGifContainer();
}

void EmotionDisplay::HideUnnecessaryElements()
{
    DisplayLockGuard lock(this);

    // 隐藏父类创建的所有UI元素，只在屏幕上留下表情容器
    auto screen = lv_screen_active();

    // 遍历父类创建的UI元素并隐藏它们
    uint32_t child_count = lv_obj_get_child_count(screen);
    for (uint32_t i = 0; i < child_count; i++)
    {
        lv_obj_t *child = lv_obj_get_child(screen, i);
        if (child && child != emotion_content_)
        {
            lv_obj_add_flag(child, LV_OBJ_FLAG_HIDDEN);
        }
    }

    ESP_LOGI(TAG, "隐藏UI元素完成");
}

void EmotionDisplay::SetupGifContainer()
{
    DisplayLockGuard lock(this);

    // 直接在屏幕上创建表情容器，而不是在content中
    auto screen = lv_screen_active();
    emotion_content_ = lv_obj_create(screen);

    // 占满整个屏幕
    lv_obj_set_size(emotion_content_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_opa(emotion_content_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(emotion_content_, 0, 0);
    lv_obj_set_style_pad_all(emotion_content_, 0, 0);
    lv_obj_set_scrollbar_mode(emotion_content_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_center(emotion_content_);

    // 创建表情GIF组件
    emotion_gif_ = lv_gif_create(emotion_content_);

    // 添加颜色反转样式
    // lv_obj_set_style_img_recolor_opa(emotion_gif_, LV_OPA_COVER, 0);
    // lv_obj_set_style_img_recolor(emotion_gif_, lv_color_white(), 0);

    // 设置差值混合模式实现颜色反转
    lv_obj_set_style_blend_mode(emotion_gif_, LV_BLEND_MODE_DIFFERENCE, 0);

    // 设置容器背景为白色，用于差值计算
    lv_obj_set_style_bg_color(emotion_content_, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(emotion_content_, LV_OPA_COVER, 0);

    // 直接设置表情容器为128x64
    lv_obj_set_size(emotion_gif_, 128, 64);
    lv_obj_set_style_border_width(emotion_gif_, 0, 0);
    lv_obj_set_style_bg_opa(emotion_gif_, LV_OPA_TRANSP, 0);

    lv_obj_center(emotion_gif_);

    // 设置默认表情
    lv_gif_set_src(emotion_gif_, &blink_once_gif);

    ESP_LOGI(TAG, "GIF表情容器设置完成");
}

void EmotionDisplay::SetEmotion(const char *emotion)
{
    if (!emotion || !emotion_gif_)
    {
        return;
    }

    DisplayLockGuard lock(this);

    // 查找对应的GIF
    for (const auto &map : emotion_maps_)
    {
        if (map.name && strcmp(map.name, emotion) == 0)
        {
            lv_gif_set_src(emotion_gif_, map.gif);
            ESP_LOGI(TAG, "设置OLED表情: %s", emotion);
            return;
        }
    }

    // 如果没有找到匹配的表情，使用默认表情
    lv_gif_set_src(emotion_gif_, &blink_once_gif);
    ESP_LOGI(TAG, "未知表情'%s'，使用默认表情", emotion);
}

void EmotionDisplay::SetChatMessage(const char *role, const char *content)
{
    // 不显示聊天消息，直接返回
    return;
}