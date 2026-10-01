
#ifndef SHOP_ONCE
#define SHOP_ONCE

// C programming is so astronomically painful bro 
typedef struct Shop Shop;

#ifndef CSV_SQL
    // this is kinda just here for reference, `CSV_SQL` should be defined in `shop.c` before `#include "shop.h"` 
    typedef struct {
        char **columns,**cells;
        int width,height;
        char* error;
    } SQL_Result;

    // We took the bounds checker in and gave him a cartel execution
    #define CELL(r, x, y) ((r)->cells[(y)*(r)->width+(x)])
#endif

typedef struct {
    size_t count,capacity;
    char* str;
} SQL_Template;

#define NEW_SQL (SQL_Template){0,8,NULL}

#define APPEND_SQL(t, ...) do {\
    int needed = snprintf(NULL, 0, __VA_ARGS__);\
    if (t.count + (size_t)needed + 1 > t.capacity) {\
        while (t.count + (size_t)needed + 1 > t.capacity) {\
            t.capacity *= 2;\
        }\
        t.str = realloc(t.str, t.capacity);\
    }\
    t.count += snprintf(t.str + t.count, t.capacity - t.count, __VA_ARGS__);\
} while (0)

// not technically individual free because dynamic arrays are the PRECURSOR to arenas
#define NUKE_SQL(t) free(t.str)

typedef struct {
    char **names, **types;
    int count, capacity;
    Arena alloc;
} Schema_List;

typedef struct {
    char* data;
    size_t count,capacity;
    bool dirty;
    Arena alloc;
} Text_Editor;

typedef struct {
    Text_Editor* current_ed;
    float split_h,split_v;
    sqlite3* db;
    SQL_Result prev_result; 
    Schema_List schema;
    bool active;
    char csv_path_buf[500];

    Arena alloc;
    Arena temp;
} Admin_Panel;

typedef struct {
    bool active;
    char keywords[100];
    char category[100];
    float min_price,max_price;
    int min_stock;
    bool use_min,use_max,use_stock,descending;
} Adv_Search_Panel;


typedef struct {
    bool success;
    int user_id;
    char username[100];
    char email[160];
    char message[256];
} Auth_Result;

typedef struct {
    bool register_mode;
    bool forgot_password_mode;
    bool logged_in;
    bool show_password;
    int user_id;

    char username[100];
    char email[160];
    char password[100];

    char current_username[100];
    char current_email[160];
    char current_password[100];
    char new_password[100];
    char confirm_password[100];

    char message[256];
    char settings_message[256];
} Account_State;

typedef enum {
    LOAD_SCREEN,
    HOME_SCREEN,
    DISPLAY_SCREEN,
    ACCOUNT_SCREEN,
    CART_SCREEN,
    CHECKOUT_SCREEN,
} Screen;

typedef void (*Home_Button_Callback)(Shop *shop);

typedef struct {
    Texture2D texture;
    Screen transition;
    Home_Button_Callback callback;
} Home_Button;

typedef struct {
    int id;
    char name[100];
    char description[500];
    float price;
    int stock;
    char category[100];
    char display[100];
} Item;

typedef struct {
    Item* items;
    int count, capacity;
    Arena alloc;
} Item_List;

typedef struct {
    bool is_model;
    float scale;
    union {
        Texture2D texture;
        Model model;
    };
} Item_Resource;

typedef Ht(int, Item_Resource) Item_Resource_Table;

typedef struct Shop {
    Admin_Panel admin;
    Adv_Search_Panel adv_search;
    Account_State account;

    Item_Resource_Table item_resources;
    bool paused;
    char search_bar[100];
    bool search_bar_active;
    float search_flash;

    // HOME
    Home_Button* home_buttons;
    int home_button_count;
    Item_List featured;
    float top_row_scroll, top_row_scroll_target;
    float bottom_row_scroll, bottom_row_scroll_target;

    // DISPLAY
    Item_List display;
    float scroll, scroll_target;

    // CORE
    Camera3D camera;
    RenderTexture2D render_target;
    Shader shader;
    int time_loc; // cache this for perf
    Screen screen;
    bool transitioning;
    float transition_alpha;
    bool fading_out;
    Screen transition_target;
} Shop;

void strip_file_name(char *path);

void shop_render_pass(Shop* shop);
void ui_render_pass(Shop* shop);
void screen_swap(Shop* shop, Screen screen);
Vector2 mouse_pos_in_shop(Shop* shop);
Vector2 mouse_yaw_pitch(Shop* shop);
void update_carousel(float* scroll, float* target, int count, float spacing, bool active);
void draw_carousel(Shop* shop, Item_List* items, float scroll, float spacing, float y);
int carousel_focused_item_index(Item_List items, float scroll, float spacing);
float carousel_item_alpha(Item_List items, float scroll, float spacing);
Item_List query_items(Shop* shop, const char* sql);
void reset_item_list(Item_List* list);
void draw_button(Shop* shop, Rectangle bounds, const char* label, Color color);
bool button_event(Shop* shop, Rectangle bounds);
Rectangle back_button_bounds(Shop* shop);

bool auth_init(Shop* shop);
Auth_Result auth_login(Shop* shop, const char* username, const char* password);
Auth_Result auth_register(Shop* shop, const char* username, const char* password);
void auth_logout(Shop* shop);
void update_account(Shop* shop);
void draw_account(Shop* shop);
void account_panel(Shop* shop);

bool init_shop(Shop *shop);
void update_shop(Shop *shop);
void draw_shop(Shop *shop);

#define ITEM_ID_COLUMN       0
#define ITEM_NAME_COLUMN     1
#define ITEM_DESC_COLUMN     2
#define ITEM_PRICE_COLUMN    3
#define ITEM_STOCK_COLUMN    4
#define ITEM_CATEGORY_COLUMN 5
#define ITEM_DISPLAY_COLUMN  6

#define ITEM_COLUMNS "id, name, description, price, stock, category, display"

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#endif
