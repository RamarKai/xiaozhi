#pragma once

#include <libs/gif/lv_gif.h>
#include "display/oled_display.h"

// 包含GIF图像数据头文件
#include "gif/anger.h"
#include "gif/blink_once.h"
#include "gif/blink_twice.h"
#include "gif/disdain.h"
#include "gif/excited.h"
#include "gif/fear.h"
#include "gif/look_left.h"
#include "gif/look_right.h"
#include "gif/sad.h"
#include "gif/nomal.h"
#include "gif/sleep.h"

/**
 * @brief OLED GIF表情显示类
 * 继承OledDisplay，添加GIF表情支持
 */
class EmotionDisplay : public OledDisplay
{
public:
    /**
     * @brief 构造函数
     */
    EmotionDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                   int width, int height, bool mirror_x, bool mirror_y);

    virtual ~EmotionDisplay() = default;

    // 重写表情设置方法
    virtual void SetEmotion(const char *emotion) override;

    // 重写聊天消息设置方法
    virtual void SetChatMessage(const char *role, const char *content) override;

private:
    void SetupGifContainer();
    void HideUnnecessaryElements();

    lv_obj_t *emotion_gif_;     ///< GIF表情组件
    lv_obj_t *emotion_content_; ///< 表情内容容器

    // 表情映射结构
    struct EmotionMap
    {
        const char *name;
        const lv_image_dsc_t *gif;
    };

    static const EmotionMap emotion_maps_[];
};
