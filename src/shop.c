
#include "raylib.h"
#include "rlgl.h" // RLGL_IMPLEMENTATION is not needed and causes linkage error!
#include "raymath.h"
#include "cimgui.h"
#include "rlImGui.h"
#include "sqlite3.h"

#ifdef PLATFORM_WEB
#include "emscripten.h"
#endif

#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "float.h"

#define ARENA_IMPLEMENTATION
#include "arena.h"
#define HT_IMPLEMENTATION
#include "ht.h"
#define CSV_SQL 
#define CSV_IMPLEMENTATION
#include "csv.h"

#include "shop.h"

#include "assets.c"
#include "shaders.c"
#include "home.c"
#include "display.c"
#include "admin.c"
#include "adv_search.c"
#include "account.c"
#include "web_clipboard.c"

int main(int argc, char* argv[]) {
    // Global setup, and rlImGui
	int screen_width = 1280;
	int screen_height = 800;
	SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
	InitWindow(screen_width, screen_height, "shop");
	SetTargetFPS(144);
    SetExitKey(KEY_NULL);
    init_textures();
	rlImGuiSetup(true);

#ifdef PLATFORM_WEB
    // clipboard hack for web
    ImGuiPlatformIO* pio = ImGui_GetPlatformIO();
    pio->Platform_GetClipboardTextFn = web_sync_clipboard_is_fricked;
    pio->Platform_SetClipboardTextFn = web_set_clipboard;
    install_paste_hook();
#endif

    Shop shop = {0};
    bool ok = init_shop(&shop);
    if (!ok) {
        return 1;
    }

	while (!WindowShouldClose()) {
        // INPUT
        bool admin_shortcut = (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_A);
        if (admin_shortcut) {
            shop.admin.active = !shop.admin.active;
        }
        shop.paused = shop.admin.active || shop.adv_search.active;
        
        // UPDATE
        if (!shop.paused) {
            update_shop(&shop);
        }

        // DRAW
        BeginTextureMode(shop.render_target);
            draw_shop(&shop);
        EndTextureMode();
		BeginDrawing();
            ClearBackground(BLACK);
            shop_render_pass(&shop);
            ui_render_pass(&shop);
		EndDrawing();
	}

    sqlite3_close(shop.admin.db);
    arena_free(&shop.admin.current_ed->alloc);
    rlImGuiShutdown();
	CloseWindow();
}

void strip_file_name(char *path) {
    char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    memmove(path, name, strlen(name) + 1);
    char *dot = strrchr(path, '.');
    if (dot) {
        *dot = '\0';
    }
}

void shop_render_pass(Shop* shop) {
    int time_loc = shop->time_loc;
    float time = (float)GetTime();
    RenderTexture2D target = shop->render_target;
    Shader shader = shop->shader;
    SetShaderValue(shader, time_loc, &time, SHADER_UNIFORM_FLOAT);

    float window_w = (float)GetScreenWidth();
    float window_h = (float)GetScreenHeight();
    float target_w = (float)target.texture.width;
    float target_h = (float)target.texture.height;
    float scale = fminf(window_w / target_w, window_h / target_h);
    float draw_w = target_w * scale;
    float draw_h = target_h * scale;

    BeginShaderMode(shader);

    Rectangle source = { 0,0,target_w,-target_h };
    Rectangle dest = { 
        (window_w - draw_w) * 0.5, (window_h - draw_h) * 0.5, 
        draw_w, draw_h
    };
    DrawTexturePro(
        target.texture,
        source,
        dest,
        (Vector2){0, 0},
        0.0f,
        WHITE
    );
    EndShaderMode();
}

void ui_render_pass(Shop* shop) {
    if (shop->paused) {
        DrawRectangle(0,0, GetScreenWidth(), GetScreenHeight(),
            Fade(BLACK, 0.60)
        );
    }

    rlImGuiBegin();
#ifdef PLATFORM_WEB
    web_clipboard_flush();
#endif
    if (shop->admin.active) {
        admin_panel(&shop->admin);
    }
    if (shop->adv_search.active) {
        adv_search_panel(shop, &shop->adv_search);
    }
    if (shop->screen == ACCOUNT_SCREEN && !shop->admin.active && !shop->adv_search.active) {
        account_panel(shop);
    }
    rlImGuiEnd();
}

void screen_swap(Shop* shop, Screen target) {
    if (shop->transitioning) return;
    shop->transitioning = true;
    shop->fading_out = true;
    shop->transition_target = target;
    shop->transition_alpha = 0.0;
}

Vector2 mouse_pos_in_shop(Shop* shop) {
    Vector2 mouse = GetMousePosition();
    float window_w = (float)GetScreenWidth();
    float window_h = (float)GetScreenHeight();
    float target_w = (float)shop->render_target.texture.width;
    float target_h = (float)shop->render_target.texture.height;
    float scale = fminf(window_w/target_w, window_h/target_h);
    float draw_w = target_w * scale;
    float draw_h = target_h * scale;
    float offset_x = (window_w - draw_w) * 0.5;
    float offset_y = (window_h - draw_h) * 0.5;
    return (Vector2) {
        (mouse.x - offset_x) / scale,
        (mouse.y - offset_y) / scale
    };
}

Vector2 mouse_yaw_pitch(Shop* shop) {
    Vector2 mouse = mouse_pos_in_shop(shop);
    float screen_w = shop->render_target.texture.width;
    float screen_h = shop->render_target.texture.height;
    float mouse_x = (mouse.x / screen_w - 0.5) * 2.0;
    float mouse_y = (mouse.y / screen_h - 0.5) * 2.0;
    mouse_x = Clamp(mouse_x, -1.0, 1.0);
    mouse_y = Clamp(mouse_y, -1.0, 1.0);
    float yaw   = mouse_x * 8.0;
    float pitch = mouse_y * 5.0;
    return (Vector2){ yaw, pitch };
}

void update_carousel(float* scroll, float* target, int count, float spacing, bool active) {
    if (active) {
        float wheel = GetMouseWheelMove();
        *target -= wheel * spacing;
        if (IsKeyPressed(KEY_RIGHT)) {
            *target += spacing;
        }
        if (IsKeyPressed(KEY_LEFT)) {
            *target -= spacing;
        }
    }
    float max_scroll = fmaxf(0.0, (count-1) * spacing);
    *target = Clamp(*target, 0.0, max_scroll);
    *scroll = Lerp(*scroll, *target, 1.0 - powf(0.001, GetFrameTime()));
}

void draw_carousel(Shop* shop, Item_List* items, float scroll, float spacing, float y) {
    for (size_t i = 0; i < items->count; i++) {
        Item item = items->items[i];
        Item_Resource* resource = get_item_resource(shop, item); 
        if (!resource) {
            continue;
        }
        float item_x = i * spacing;
        float center_dist = fabsf(item_x - scroll);
        float focus = 1.0 - Clamp(
            center_dist / spacing,
            0.0, 1.0
        );
        float focus_scale = Lerp(0.65, 1.15, focus);
        Vector3 pos = {
            item_x,
            y + Lerp(-0.25, 0.0, focus),
            Lerp(1.5, 0.0, focus)
        };
        if (resource->is_model) {
            float spin = GetTime() * 25.0 + i * 47.0;
            DrawModelEx(
                resource->model,
                pos,
                (Vector3){ 0.2, 1.0, 0.2 },
                spin,
                (Vector3) {
                    resource->scale * focus_scale,
                    resource->scale * focus_scale,
                    resource->scale * focus_scale
                },
                WHITE
            );
        } else {
            Rectangle source = {
                0.0,0.0,
                (float)resource->texture.width,(float)resource->texture.height
            };
            float scale = resource->scale * focus_scale;
            float size_y = ((float)resource->texture.height / (float)resource->texture.width);
            Vector2 size = {
                scale,
                scale * size_y
            };
            Vector2 origin = {
                size.x * 0.5,0
            };

            DrawBillboardPro(
                shop->camera,
                resource->texture,
                source,
                pos,
                (Vector3){ 0.0, 1.0, 0.0},
                size,
                origin,
                0.0,
                WHITE
            );
        }
    }
}

int carousel_focused_item_index(Item_List items, float scroll, float spacing) {
    int index = (int)roundf(scroll / spacing);
    index = MIN(index, items.count-1);
    return index;
}

float carousel_item_alpha(Item_List items, float scroll, float spacing) {
    int index = carousel_focused_item_index(items, scroll, spacing);
    if (index < 0) {
        index = 0;
    }
    if (index >= (int)items.count) {
        index = (int)items.count - 1;
    }
    float item_x = index * spacing;
    float dist = fabsf(scroll - item_x);
    float alpha = 1.0 - Clamp(dist / (spacing * 0.35), 0.0, 1.0);
    return alpha;
}

// NOTE!!
// the sql must be some form of `"SELECT " ITEM_COLUMNS " FROM items "`
// or else BAD THINGS WILL HAPPEN!!!
Item_List query_items(Shop* shop, const char* sql) {
    Item_List list = {0};
    SQL_Result result = sql_run(&shop->admin, (char*)sql);
    if (result.error) {
        printf("%s\n", result.error);
        return list;
    }

    for (int y=0; y<result.height; y++) {
        Item item = {0};
        item.id = atoi(CELL(&result, 0, y));
        snprintf(item.name, sizeof(item.name), "%s", 
            CELL(&result, ITEM_NAME_COLUMN, y)
        );
        snprintf(item.description, sizeof(item.description), "%s", 
            CELL(&result, ITEM_DESC_COLUMN, y)
        );
        item.price = strtof(CELL(&result, ITEM_PRICE_COLUMN, y), NULL);
        item.stock = atoi(CELL(&result, ITEM_STOCK_COLUMN, y));
        snprintf(item.category, sizeof(item.category), "%s", 
            CELL(&result, ITEM_CATEGORY_COLUMN, y)
        );
        snprintf(item.display, sizeof(item.display), "%s", 
            CELL(&result, ITEM_DISPLAY_COLUMN, y)
        );

        arena_da_append(&list.alloc, &list, item);
    }
    return list;
}

void reset_item_list(Item_List* list) {
    arena_reset(&list->alloc);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

/* REUSABLE UI "COMPONENTS" */
void draw_button(Shop* shop, Rectangle bounds, const char* label, Color color) {
    Vector2 mouse = mouse_pos_in_shop(shop);
    bool hovered = CheckCollisionPointRec(mouse, bounds);
    
    float darken = IsMouseButtonDown(MOUSE_BUTTON_LEFT) ? -0.8 : -0.5;
    DrawRectangleRounded(bounds, 0.2, 8, 
        hovered ? ColorBrightness(color, darken) : color 
    );

    int font_size = 20;
    int label_w = MeasureText(label, font_size);
    DrawText(
        label,
        bounds.x + (bounds.width - label_w) * 0.5f,
        bounds.y + (bounds.height - font_size) * 0.5f,
        font_size,
        BLACK
    );
}

bool button_event(Shop* shop, Rectangle bounds) {
    Vector2 mouse = mouse_pos_in_shop(shop);
    if (CheckCollisionPointRec(mouse, bounds) && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        return true;
    }
    return false;
}

/* BACK BUTTON */
Rectangle back_button_bounds(Shop* shop) {
    float screen_h = shop->render_target.texture.height;
    return (Rectangle){
        24.0, screen_h - 100.0, 120.0, 80.0
    };
}

void back_button_event(Shop* shop) {
    Rectangle back = back_button_bounds(shop);
    Vector2 mouse = mouse_pos_in_shop(shop);
    if (CheckCollisionPointRec(mouse, back) && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        screen_swap(shop, HOME_SCREEN);
    }
}

/* SEARCH BAR */
Rectangle advanced_search_button_bounds(Shop* shop) {
    float screen_w = shop->render_target.texture.width;
    return (Rectangle){ screen_w - 24.0 - 80.0, 24.0, 80.0, 60.0 };
}

Rectangle search_bar_bounds(Shop* shop) {
    Rectangle advanced = advanced_search_button_bounds(shop);
    return (Rectangle){ advanced.x - 8.0 - 450.0, 24.0, 450.0, 60.0 };
}

bool search_bar_event(Shop* shop) {
    Rectangle bar = search_bar_bounds(shop);
    Vector2 mouse = mouse_pos_in_shop(shop);

    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        shop->search_bar_active = CheckCollisionPointRec(mouse, bar);
    }
    if (!shop->search_bar_active) {
        return false;
    }
    int c = GetCharPressed();
    while (c > 0) {
        if (c >= 32 && c <= 126) {
            size_t len = strlen(shop->search_bar);
            if (len + 1 < sizeof(shop->search_bar)) {
                shop->search_bar[len] = (char)c;
                shop->search_bar[len + 1] = '\0';
            }
        }
        c = GetCharPressed();
    }

    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) {
        size_t len = strlen(shop->search_bar);
        if (len > 0) {
            shop->search_bar[len - 1] = '\0';
        }
    }
    return IsKeyPressed(KEY_ENTER);
}

void draw_search_bar(Shop* shop) {
    Rectangle bar = search_bar_bounds(shop);
    Vector2 mouse = mouse_pos_in_shop(shop);
    bool hovered = CheckCollisionPointRec(mouse, bar);
    Color outline = SHOP_SHADOW;
    if (shop->search_bar_active) {
        outline = SHOP_GREEN;
    } else if (hovered) {
        outline = ColorBrightness(SHOP_GREEN, -0.25);
    }
    DrawRectangleRounded(bar, 0.25, 8, outline);

    float padding = 16.0;
    float cursor_padding = 4.0;
    float font_size = 24.0;
    float spacing = 1.0;
    float cursor_width = 2.0;

    Rectangle text_area = { bar.x + padding, bar.y, bar.width - padding * 2.0, bar.height };
    Vector2 text_size = MeasureTextEx(SHOP_FONT, shop->search_bar, font_size, spacing);
    float overflow = text_size.x + cursor_padding - text_area.width;
    float text_offset = 0.0;
    if (overflow > 0.0) {
        text_offset = overflow;
    }
    float text_x = text_area.x - text_offset;
    float text_y = bar.y + (bar.height - font_size) * 0.5 - 2.0;
    BeginScissorMode(
        (int)text_area.x, (int)text_area.y, 
        (int)text_area.width, (int)text_area.height
    );
    if (shop->search_bar[0] != '\0') {
        DrawTextEx(
            SHOP_FONT,
            shop->search_bar,
            (Vector2){ text_x, text_y },
            font_size,
            spacing,
            SHOP_INK
        );
    } else if (!shop->search_bar_active) {
        DrawTextEx(
            SHOP_FONT,
            "Search...",
            (Vector2){ text_x, text_y },
            font_size,
            spacing,
            Fade(SHOP_INK, 0.5)
        );
    }
    EndScissorMode();

    if (shop->search_bar_active) {
        float cursor_x = text_x + text_size.x + cursor_padding;
        if (fmod(GetTime(), 1.0) < 0.5) {
            DrawRectangle(
                (int)cursor_x, (int)(bar.y + 14.0f),
                cursor_width,(int)(bar.height - 28.0f),
                SHOP_INK
            );
        }
    }
}

bool init_shop(Shop *shop) {
    // Admin UI Setup
    Admin_Panel* admin = &shop->admin;
    Text_Editor* ed = (Text_Editor*)malloc(sizeof(Text_Editor));
    init_text_ed(ed);
    init_admin_panel(admin, ed);

    // Use a file-backed SQLite database on desktop so local accounts, carts,
    // discounts, and orders can persist between runs. The web build will use
    // a server/API later, so it remains in-memory for now.
#ifdef PLATFORM_WEB
    int database_result = sqlite3_open(":memory:", &admin->db);
#else
    int database_result = sqlite3_open("shop.db", &admin->db);
#endif
    if (database_result != SQLITE_OK) {
        printf("sqlite open failed: `%s`\n", sqlite3_errmsg(admin->db));
        return false;
    }

    // Import the product CSV only when the existing item table is missing.
    // This avoids recreating product data every time the persistent database opens.
    sqlite3_stmt* item_table_check = NULL;
    bool item_table_exists = false;
    const char* item_table_query =
        "SELECT 1 FROM sqlite_master WHERE type='table' AND name='items' LIMIT 1;";

    if (sqlite3_prepare_v2(admin->db, item_table_query, -1, &item_table_check, NULL) == SQLITE_OK) {
        item_table_exists = sqlite3_step(item_table_check) == SQLITE_ROW;
    }
    sqlite3_finalize(item_table_check);

    if (!item_table_exists) {
        char item_csv_path[] = "assets/csv/items.csv";
        SQL_Result csv_result = admin_load_csv(admin, item_csv_path);
        if (csv_result.error) {
            printf("admin_load_csv failed: `%s`\n", csv_result.error);
            return false;
        }

        SQL_Result typed_result = sql_run(admin,
            "CREATE TABLE items_typed ("
                "id INTEGER PRIMARY KEY,"
                "name TEXT NOT NULL,"
                "description TEXT NOT NULL,"
                "price REAL NOT NULL CHECK(price >= 0),"
                "stock INTEGER NOT NULL CHECK(stock >= 0),"
                "category TEXT NOT NULL,"
                "display TEXT NOT NULL"
            ");"
            "INSERT INTO items_typed "
            "SELECT CAST(id AS INTEGER), name, description, CAST(price AS REAL), "
                   "CAST(stock AS INTEGER), category, display FROM items;"
            "DROP TABLE items;"
            "ALTER TABLE items_typed RENAME TO items;"
        );

        if (typed_result.error) {
            printf("csv type coerce failed: `%s`\n", typed_result.error);
            return false;
        }
    }

    if (!auth_init(shop)) {
        printf("failed to initialize account/cart/order database tables\n");
        return false;
    }
    schema_list_refresh(admin);

    // Renderer setup
    int render_width = 1000;
    int render_height = 800;
    RenderTexture2D target = LoadRenderTexture(render_width, render_height);
    shop->shader = LoadShaderFromMemory(VERTEX, FRAGMENT);
    int resolution_loc = GetShaderLocation(shop->shader, "resolution");
    shop->time_loc = GetShaderLocation(shop->shader, "time");
    Vector2 resolution = {(float)target.texture.width, (float)target.texture.height};
    SetShaderValue(shop->shader, resolution_loc, &resolution, SHADER_UNIFORM_VEC2);
    shop->render_target = target;
    shop->camera.position = (Vector3){ 0.0, 0.0, 7.0 };
    shop->camera.target   = (Vector3){ 0.0, 0.0, 0.0 };
    shop->camera.up       = (Vector3){ 0.0, 1.0, 0.0 };
    shop->camera.fovy     = 45.0;
    shop->camera.projection = CAMERA_PERSPECTIVE;

    // actual Shop setup
    shop->screen = LOAD_SCREEN;
    init_home_menu_buttons(shop);
    // inline SQL is CRAZY
    // it looks terrible, bust trust me, inline GCC assembly is far worse!
    shop->featured = query_items(shop,
        "SELECT " ITEM_COLUMNS " FROM items "
        "WHERE name IN ("
            "'wooden_chair', "
            "'caveman_chair', "
            "'the_batmobile', "
            "'lawn_chair', "
            "'refrigerator', "
            "'kingly_throne'"
        ") "
        "ORDER BY CASE name "
            "WHEN 'wooden_chair'  THEN 0 "
            "WHEN 'caveman_chair' THEN 1 "
            "WHEN 'the_batmobile' THEN 2 "
            "WHEN 'lawn_chair'    THEN 3 "
            "WHEN 'refrigerator'  THEN 4 "
            "WHEN 'kingly_throne' THEN 5 "
        "END;"
    );
    shop->paused = false;

    return true;
}

void update_shop(Shop *shop) {
    float dt = GetFrameTime();
    if (shop->transitioning) {
        float speed = 1.5;
        if (shop->fading_out) {
            shop->transition_alpha += speed * dt;
            if (shop->transition_alpha >= 1.0) {
                shop->transition_alpha = 1.0;
                shop->screen = shop->transition_target;
                shop->fading_out = false;
            }
        } else {
            shop->transition_alpha -= speed * dt;
            if (shop->transition_alpha <= 0.0) {
                shop->transition_alpha = 0.0;
                shop->transitioning = false;
            }
        }
        return;
    }
    if (shop->search_flash > 0.0) {
        shop->search_flash -= 4.0 * dt;
        if (shop->search_flash < 0.0) {
            shop->search_flash = 0.0;
        }
    }

    switch (shop->screen) {
    case LOAD_SCREEN:
        break;

    case HOME_SCREEN:
        update_home(shop);
        if (search_bar_event(shop)) {
            const char* sql = sqlite3_mprintf(
                "SELECT " ITEM_COLUMNS " FROM items "
                "WHERE display COLLATE NOCASE LIKE '%%%q%%' "
                "ORDER BY display;",
                shop->search_bar
            );
            reset_item_list(&shop->display);
            shop->display = query_items(shop, sql);
            shop->scroll = 0.0;
            shop->scroll_target = 0.0;
            sqlite3_free((void*)sql);
            screen_swap(shop, DISPLAY_SCREEN);
        }
        if (button_event(shop, advanced_search_button_bounds(shop))) {
            shop->adv_search.active = true;
        }
        break;

    case ACCOUNT_SCREEN:
        update_account(shop);
        break;

    case DISPLAY_SCREEN:
        update_display(shop);
        if (button_event(shop, back_button_bounds(shop))) {
            screen_swap(shop, HOME_SCREEN);
        }
        if (search_bar_event(shop)) {
            const char* sql = sqlite3_mprintf(
                "SELECT " ITEM_COLUMNS " FROM items "
                "WHERE display COLLATE NOCASE LIKE '%%%q%%' "
                "ORDER BY display;",
                shop->search_bar
            );
            reset_item_list(&shop->display);
            shop->display = query_items(shop, sql);
            shop->scroll = 0.0;
            shop->scroll_target = 0.0;
            sqlite3_free((void*)sql);
            shop->search_flash = 1.0;
        }
        if (button_event(shop, advanced_search_button_bounds(shop))) {
            shop->adv_search.active = true;
        }
        break;
    }
}

void draw_shop(Shop *shop) {
    float screen_w = shop->render_target.texture.width;
    float screen_h = shop->render_target.texture.height;

    switch (shop->screen) {
    case LOAD_SCREEN: {
        ClearBackground((Color){ 230,230,230,255 });
        // calculate logo centering and draw 
        float logo_area_h = screen_h - 140.0;
        float scale = fminf(screen_w/(float)LOGO.width, logo_area_h/(float)LOGO.height);
        scale *= 0.8;
        float draw_w = LOGO.width * scale;
        float draw_h = LOGO.height * scale;
        Rectangle source = { 0,0, (float)LOGO.width, (float)LOGO.height };
        Rectangle dest = { 
            (screen_w - draw_w)/2, (logo_area_h - draw_h)/2, 
            draw_w, draw_h 
        };
        DrawTexturePro(LOGO, source, dest, (Vector2){0,0}, 0.0, WHITE);

        // goofy loading bar
        float bar_w = 800.0; 
        float bar_h = 30.0; 
        float bar_x = (screen_w - bar_w) * 0.5; 
        float bar_y = screen_h - 130.0; 
        float elapsed = (float)GetTime();
        // `static` is incredibly good for throwaway gags
        static float progress = 0.0;
        static float target = 0.0;
        if (elapsed < 4.0) {
            if (fabsf(progress - target) < 0.02) {
                target = (float)GetRandomValue(10, 90) / 100.0;
            }
        } else {
            target = 1.0;
        }
        progress = Lerp(progress, target, 3.0 * GetFrameTime());
        DrawRectangleRounded(
            (Rectangle){bar_x, bar_y, bar_w * progress, bar_h},
            1.0, 5, SHOP_GREEN
        );
        if (elapsed > 5.0 || IsKeyPressed(KEY_ESCAPE)) {
            screen_swap(shop, HOME_SCREEN);
        }
    }break;

    case HOME_SCREEN:
        ClearBackground(SHOP_BG);
        draw_home(shop);
        draw_search_bar(shop);
        draw_button(shop, advanced_search_button_bounds(shop), "Adv.", SHOP_BLUE);
        break;

    case ACCOUNT_SCREEN:
        ClearBackground(SHOP_BG);
        draw_account(shop);
        break;

    case DISPLAY_SCREEN:
        ClearBackground(SHOP_BG);
        draw_display(shop);
        draw_search_bar(shop);
        draw_button(shop, advanced_search_button_bounds(shop), "Adv.", SHOP_BLUE);
        draw_button(shop, back_button_bounds(shop), "< HOME", SHOP_GREEN);
        break;
    }

    if (shop->transition_alpha > 0.0) {
        DrawRectangle(0,0, screen_w,screen_h, Fade(BLACK, shop->transition_alpha));
    }
    if (shop->search_flash > 0.0f) {
        DrawRectangle(0,0, screen_w,screen_h, Fade(RAYWHITE, shop->search_flash));
    }
}
