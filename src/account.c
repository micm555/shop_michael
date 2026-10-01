/*
 * Local account system for the desktop build.
 *
 * The UI calls the functions in this file instead of writing SQL directly.
 * Later, these functions can be replaced with server/API requests while the
 * account screen stays mostly the same.
 *
 * NOTE: password_hash is only a placeholder column name in this local class
 * prototype. The local version still stores the test password directly.
 * A real server must hash passwords before storing them.
 */

static bool account_run_sql(Shop* shop, const char* sql) {
    char* database_error = NULL;
    int result = sqlite3_exec(shop->admin.db, sql, NULL, NULL, &database_error);

    if (result != SQLITE_OK) {
        printf("account database error: %s\n",
               database_error ? database_error : sqlite3_errmsg(shop->admin.db));
        sqlite3_free(database_error);
        return false;
    }

    return true;
}

bool auth_init(Shop* shop) {
    const char* account_schema =
        "PRAGMA foreign_keys = ON;"

        "CREATE TABLE IF NOT EXISTS users ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "username TEXT UNIQUE NOT NULL COLLATE NOCASE,"
            "email TEXT UNIQUE,"
            "password_hash TEXT NOT NULL,"
            "is_admin INTEGER NOT NULL DEFAULT 0,"
            "created_at TEXT DEFAULT CURRENT_TIMESTAMP"
        ");"

        "CREATE TABLE IF NOT EXISTS carts ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "user_id INTEGER UNIQUE NOT NULL,"
            "created_at TEXT DEFAULT CURRENT_TIMESTAMP,"
            "FOREIGN KEY (user_id) REFERENCES users(id)"
        ");"

        "CREATE TABLE IF NOT EXISTS cart_items ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "cart_id INTEGER NOT NULL,"
            "product_id INTEGER NOT NULL,"
            "quantity INTEGER NOT NULL DEFAULT 1,"
            "FOREIGN KEY (cart_id) REFERENCES carts(id)"
        ");"

        "CREATE TABLE IF NOT EXISTS discount_codes ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "code TEXT UNIQUE NOT NULL,"
            "discount_type TEXT NOT NULL,"
            "discount_value NUMERIC NOT NULL,"
            "is_active INTEGER NOT NULL DEFAULT 1,"
            "expiration_date TEXT,"
            "created_at TEXT DEFAULT CURRENT_TIMESTAMP"
        ");"

        "CREATE TABLE IF NOT EXISTS orders ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "user_id INTEGER NOT NULL,"
            "discount_code_id INTEGER,"
            "subtotal NUMERIC NOT NULL,"
            "discount_amount NUMERIC NOT NULL DEFAULT 0,"
            "tax_amount NUMERIC NOT NULL,"
            "total_amount NUMERIC NOT NULL,"
            "created_at TEXT DEFAULT CURRENT_TIMESTAMP,"
            "FOREIGN KEY (user_id) REFERENCES users(id),"
            "FOREIGN KEY (discount_code_id) REFERENCES discount_codes(id)"
        ");"

        "CREATE TABLE IF NOT EXISTS order_items ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "order_id INTEGER NOT NULL,"
            "product_id INTEGER NOT NULL,"
            "product_name TEXT NOT NULL,"
            "quantity INTEGER NOT NULL,"
            "unit_price NUMERIC NOT NULL,"
            "FOREIGN KEY (order_id) REFERENCES orders(id)"
        ");";

    return account_run_sql(shop, account_schema);
}

static bool username_is_valid(const char* username) {
    size_t username_length = strlen(username);
    if (username_length < 3 || username_length > 30) {
        return false;
    }

    for (size_t index = 0; index < username_length; index++) {
        char character = username[index];
        bool character_is_allowed =
            (character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') ||
            character == '_' || character == '-';

        if (!character_is_allowed) {
            return false;
        }
    }

    return true;
}

static bool email_is_valid(const char* email) {
    if (email == NULL || email[0] == '\0') {
        return false;
    }

    const char* at_symbol = strchr(email, '@');
    if (at_symbol == NULL || at_symbol == email) {
        return false;
    }

    const char* last_dot = strrchr(at_symbol + 1, '.');
    return last_dot != NULL && last_dot > at_symbol + 1 && last_dot[1] != '\0';
}

Auth_Result auth_register(Shop* shop, const char* username, const char* password) {
    Auth_Result result = {0};

    if (!username_is_valid(username)) {
        snprintf(result.message, sizeof(result.message),
                 "Username must be 3-30 characters and use letters, numbers, _ or -.");
        return result;
    }

    if (strlen(password) < 6) {
        snprintf(result.message, sizeof(result.message),
                 "Password must be at least 6 characters.");
        return result;
    }

    const char* insert_user_sql =
        "INSERT INTO users(username, email, password_hash) VALUES(?1, NULL, ?2);";

    sqlite3_stmt* statement = NULL;
    if (sqlite3_prepare_v2(shop->admin.db, insert_user_sql, -1, &statement, NULL) != SQLITE_OK) {
        snprintf(result.message, sizeof(result.message),
                 "Database error while creating the account.");
        return result;
    }

    sqlite3_bind_text(statement, 1, username, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, password, -1, SQLITE_TRANSIENT);

    int database_result = sqlite3_step(statement);

    if (database_result == SQLITE_DONE) {
        result.success = true;
        result.user_id = (int)sqlite3_last_insert_rowid(shop->admin.db);
        snprintf(result.username, sizeof(result.username), "%s", username);
        snprintf(result.message, sizeof(result.message), "Account created successfully.");
    } else if (database_result == SQLITE_CONSTRAINT) {
        snprintf(result.message, sizeof(result.message), "That username is already in use.");
    } else {
        snprintf(result.message, sizeof(result.message),
                 "Could not create account: %s", sqlite3_errmsg(shop->admin.db));
    }

    sqlite3_finalize(statement);
    return result;
}

Auth_Result auth_login(Shop* shop, const char* username, const char* password) {
    Auth_Result result = {0};

    if (username[0] == '\0' || password[0] == '\0') {
        snprintf(result.message, sizeof(result.message), "Enter a username and password.");
        return result;
    }

    const char* find_user_sql =
        "SELECT id, username, COALESCE(email, '') "
        "FROM users "
        "WHERE username = ?1 COLLATE NOCASE AND password_hash = ?2 "
        "LIMIT 1;";

    sqlite3_stmt* statement = NULL;
    if (sqlite3_prepare_v2(shop->admin.db, find_user_sql, -1, &statement, NULL) != SQLITE_OK) {
        snprintf(result.message, sizeof(result.message), "Database error while logging in.");
        return result;
    }

    sqlite3_bind_text(statement, 1, username, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, password, -1, SQLITE_TRANSIENT);

    int database_result = sqlite3_step(statement);
    if (database_result == SQLITE_ROW) {
        result.success = true;
        result.user_id = sqlite3_column_int(statement, 0);

        const unsigned char* stored_username = sqlite3_column_text(statement, 1);
        const unsigned char* stored_email = sqlite3_column_text(statement, 2);

        snprintf(result.username, sizeof(result.username), "%s",
                 stored_username ? (const char*)stored_username : username);
        snprintf(result.email, sizeof(result.email), "%s",
                 stored_email ? (const char*)stored_email : "");
        snprintf(result.message, sizeof(result.message), "Login successful.");
    } else if (database_result == SQLITE_DONE) {
        snprintf(result.message, sizeof(result.message), "Incorrect username or password.");
    } else {
        snprintf(result.message, sizeof(result.message),
                 "Login database error: %s", sqlite3_errmsg(shop->admin.db));
    }

    sqlite3_finalize(statement);
    return result;
}

static bool update_recovery_email(Shop* shop, const char* email,
                                  char* message, size_t message_size) {
    if (!email_is_valid(email)) {
        snprintf(message, message_size, "Enter a valid email address.");
        return false;
    }

    const char* update_email_sql = "UPDATE users SET email = ?1 WHERE id = ?2;";
    sqlite3_stmt* statement = NULL;

    if (sqlite3_prepare_v2(shop->admin.db, update_email_sql, -1, &statement, NULL) != SQLITE_OK) {
        snprintf(message, message_size, "Could not prepare the email update.");
        return false;
    }

    sqlite3_bind_text(statement, 1, email, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(statement, 2, shop->account.user_id);

    int database_result = sqlite3_step(statement);
    sqlite3_finalize(statement);

    if (database_result == SQLITE_CONSTRAINT) {
        snprintf(message, message_size, "That email is already used by another account.");
        return false;
    }

    if (database_result != SQLITE_DONE) {
        snprintf(message, message_size, "Could not save the recovery email.");
        return false;
    }

    snprintf(shop->account.current_email, sizeof(shop->account.current_email), "%s", email);
    snprintf(message, message_size, "Recovery email saved.");
    return true;
}

static bool change_account_password(Shop* shop,
                                    const char* current_password,
                                    const char* new_password,
                                    const char* confirmed_password,
                                    char* message,
                                    size_t message_size) {
    if (current_password[0] == '\0' || new_password[0] == '\0' || confirmed_password[0] == '\0') {
        snprintf(message, message_size, "Fill in all password fields.");
        return false;
    }

    if (strlen(new_password) < 6) {
        snprintf(message, message_size, "New password must be at least 6 characters.");
        return false;
    }

    if (strcmp(new_password, confirmed_password) != 0) {
        snprintf(message, message_size, "New passwords do not match.");
        return false;
    }

    const char* verify_password_sql =
        "SELECT 1 FROM users WHERE id = ?1 AND password_hash = ?2 LIMIT 1;";
    sqlite3_stmt* verify_statement = NULL;

    if (sqlite3_prepare_v2(shop->admin.db, verify_password_sql, -1,
                           &verify_statement, NULL) != SQLITE_OK) {
        snprintf(message, message_size, "Could not verify the current password.");
        return false;
    }

    sqlite3_bind_int(verify_statement, 1, shop->account.user_id);
    sqlite3_bind_text(verify_statement, 2, current_password, -1, SQLITE_TRANSIENT);

    bool current_password_matches = sqlite3_step(verify_statement) == SQLITE_ROW;
    sqlite3_finalize(verify_statement);

    if (!current_password_matches) {
        snprintf(message, message_size, "Current password is incorrect.");
        return false;
    }

    const char* update_password_sql =
        "UPDATE users SET password_hash = ?1 WHERE id = ?2;";
    sqlite3_stmt* update_statement = NULL;

    if (sqlite3_prepare_v2(shop->admin.db, update_password_sql, -1,
                           &update_statement, NULL) != SQLITE_OK) {
        snprintf(message, message_size, "Could not prepare the password update.");
        return false;
    }

    sqlite3_bind_text(update_statement, 1, new_password, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(update_statement, 2, shop->account.user_id);

    int database_result = sqlite3_step(update_statement);
    sqlite3_finalize(update_statement);

    if (database_result != SQLITE_DONE) {
        snprintf(message, message_size, "Could not change the password.");
        return false;
    }

    snprintf(message, message_size, "Password changed successfully.");
    return true;
}

static bool send_demo_password_reset(Shop* shop, const char* email,
                                     char* message, size_t message_size) {
    if (!email_is_valid(email)) {
        snprintf(message, message_size, "Enter a valid recovery email address.");
        return false;
    }

    const char* find_email_sql =
        "SELECT 1 FROM users WHERE email = ?1 COLLATE NOCASE LIMIT 1;";
    sqlite3_stmt* statement = NULL;

    if (sqlite3_prepare_v2(shop->admin.db, find_email_sql, -1, &statement, NULL) != SQLITE_OK) {
        snprintf(message, message_size, "Could not process the reset request right now.");
        return false;
    }

    sqlite3_bind_text(statement, 1, email, -1, SQLITE_TRANSIENT);

    /* The local demo performs the lookup but intentionally uses the same
       response whether the email exists or not. */
    sqlite3_step(statement);
    sqlite3_finalize(statement);

    snprintf(message, message_size,
             "If an account uses that recovery email, a demo reset request has been sent. "
             "No real email is sent until the server/API is added.");
    return true;
}

void auth_logout(Shop* shop) {
    Account_State* account = &shop->account;

    account->logged_in = false;
    account->user_id = 0;
    account->register_mode = false;
    account->forgot_password_mode = false;
    account->show_password = false;

    account->current_username[0] = '\0';
    account->current_email[0] = '\0';
    account->email[0] = '\0';
    account->password[0] = '\0';
    account->current_password[0] = '\0';
    account->new_password[0] = '\0';
    account->confirm_password[0] = '\0';
    account->settings_message[0] = '\0';

    snprintf(account->message, sizeof(account->message), "Logged out.");
}

static void submit_login_or_registration(Shop* shop) {
    Account_State* account = &shop->account;

    if (account->register_mode) {
        Auth_Result registration = auth_register(shop, account->username, account->password);

        if (registration.success) {
            account->register_mode = false;
            account->forgot_password_mode = false;
            account->password[0] = '\0';
            snprintf(account->message, sizeof(account->message),
                     "Account created. Log in, then add a recovery email in Account Settings.");
        } else {
            snprintf(account->message, sizeof(account->message), "%s", registration.message);
        }
        return;
    }

    Auth_Result login = auth_login(shop, account->username, account->password);
    snprintf(account->message, sizeof(account->message), "%s", login.message);

    if (!login.success) {
        return;
    }

    account->logged_in = true;
    account->forgot_password_mode = false;
    account->user_id = login.user_id;
    snprintf(account->current_username, sizeof(account->current_username), "%s", login.username);
    snprintf(account->current_email, sizeof(account->current_email), "%s", login.email);
    snprintf(account->email, sizeof(account->email), "%s", login.email);
    account->password[0] = '\0';
    account->settings_message[0] = '\0';
}

void update_account(Shop* shop) {
    (void)shop;
}

void draw_account(Shop* shop) {
    (void)shop;
}

static void center_account_text(const char* text) {
    ImVec2 text_size = ImGui_CalcTextSize(text);
    float centered_x = (ImGui_GetWindowWidth() - text_size.x) * 0.5f;

    if (centered_x > 0.0f) {
        ImGui_SetCursorPosX(centered_x);
    }

    ImGui_TextUnformatted(text);
}

static void push_account_theme(void) {
    ImGui_PushStyleVar(ImGuiStyleVar_WindowRounding, 18.0f);
    ImGui_PushStyleVar(ImGuiStyleVar_FrameRounding, 9.0f);
    ImGui_PushStyleVarImVec2(ImGuiStyleVar_WindowPadding, (ImVec2){32.0f, 26.0f});
    ImGui_PushStyleVarImVec2(ImGuiStyleVar_ItemSpacing, (ImVec2){10.0f, 10.0f});

    ImGui_PushStyleColorImVec4(ImGuiCol_WindowBg,       (ImVec4){0.055f, 0.075f, 0.105f, 0.97f});
    ImGui_PushStyleColorImVec4(ImGuiCol_Border,         (ImVec4){0.18f, 0.24f, 0.32f, 1.00f});
    ImGui_PushStyleColorImVec4(ImGuiCol_Text,           (ImVec4){0.94f, 0.96f, 0.99f, 1.00f});
    ImGui_PushStyleColorImVec4(ImGuiCol_TextDisabled,   (ImVec4){0.58f, 0.63f, 0.70f, 1.00f});
    ImGui_PushStyleColorImVec4(ImGuiCol_FrameBg,        (ImVec4){0.10f, 0.14f, 0.19f, 1.00f});
    ImGui_PushStyleColorImVec4(ImGuiCol_FrameBgHovered, (ImVec4){0.13f, 0.18f, 0.24f, 1.00f});
    ImGui_PushStyleColorImVec4(ImGuiCol_FrameBgActive,  (ImVec4){0.15f, 0.21f, 0.29f, 1.00f});
    ImGui_PushStyleColorImVec4(ImGuiCol_Button,         (ImVec4){0.11f, 0.43f, 0.93f, 1.00f});
    ImGui_PushStyleColorImVec4(ImGuiCol_ButtonHovered,  (ImVec4){0.16f, 0.50f, 1.00f, 1.00f});
    ImGui_PushStyleColorImVec4(ImGuiCol_ButtonActive,   (ImVec4){0.08f, 0.36f, 0.82f, 1.00f});
}

static void pop_account_theme(void) {
    for (int color_index = 0; color_index < 10; color_index++) {
        ImGui_PopStyleColor();
    }

    for (int style_index = 0; style_index < 4; style_index++) {
        ImGui_PopStyleVar();
    }
}

static void draw_password_visibility_button(Account_State* account, const char* button_id) {
    ImGui_SameLine();

    ImVec2 button_position = ImGui_GetCursorScreenPos();
    ImVec2 button_size = (ImVec2){40.0f, 32.0f};

    if (ImGui_InvisibleButton(button_id, button_size, 0)) {
        account->show_password = !account->show_password;
    }

    ImDrawList* draw_list = ImGui_GetWindowDrawList();
    ImU32 eye_color = ImGui_GetColorU32(ImGuiCol_Text);
    float center_x = button_position.x + 20.0f;
    float center_y = button_position.y + 16.0f;

    ImDrawList_AddLineEx(draw_list, (ImVec2){center_x - 13.0f, center_y}, (ImVec2){center_x - 6.0f, center_y - 7.0f}, eye_color, 1.8f);
    ImDrawList_AddLineEx(draw_list, (ImVec2){center_x - 6.0f, center_y - 7.0f}, (ImVec2){center_x + 6.0f, center_y - 7.0f}, eye_color, 1.8f);
    ImDrawList_AddLineEx(draw_list, (ImVec2){center_x + 6.0f, center_y - 7.0f}, (ImVec2){center_x + 13.0f, center_y}, eye_color, 1.8f);
    ImDrawList_AddLineEx(draw_list, (ImVec2){center_x - 13.0f, center_y}, (ImVec2){center_x - 6.0f, center_y + 7.0f}, eye_color, 1.8f);
    ImDrawList_AddLineEx(draw_list, (ImVec2){center_x - 6.0f, center_y + 7.0f}, (ImVec2){center_x + 6.0f, center_y + 7.0f}, eye_color, 1.8f);
    ImDrawList_AddLineEx(draw_list, (ImVec2){center_x + 6.0f, center_y + 7.0f}, (ImVec2){center_x + 13.0f, center_y}, eye_color, 1.8f);
    ImDrawList_AddCircleFilled(draw_list, (ImVec2){center_x, center_y}, 3.2f, eye_color, 12);

    if (account->show_password) {
        ImDrawList_AddLineEx(draw_list,
                             (ImVec2){center_x - 11.0f, center_y + 10.0f},
                             (ImVec2){center_x + 11.0f, center_y - 10.0f},
                             eye_color, 2.0f);
    }
}

static void draw_order_history(Shop* shop) {
    const char* order_history_sql =
        "SELECT id, total_amount, created_at "
        "FROM orders WHERE user_id = ?1 "
        "ORDER BY created_at DESC, id DESC LIMIT 5;";

    sqlite3_stmt* statement = NULL;
    if (sqlite3_prepare_v2(shop->admin.db, order_history_sql, -1, &statement, NULL) != SQLITE_OK) {
        ImGui_TextDisabled("Order history is unavailable.");
        return;
    }

    sqlite3_bind_int(statement, 1, shop->account.user_id);

    int order_count = 0;
    while (sqlite3_step(statement) == SQLITE_ROW) {
        order_count++;

        int order_id = sqlite3_column_int(statement, 0);
        double order_total = sqlite3_column_double(statement, 1);
        const unsigned char* created_at = sqlite3_column_text(statement, 2);

        ImGui_Text("Order #%d   $%.2f", order_id, order_total);
        ImGui_TextDisabled("%s", created_at ? (const char*)created_at : "");

        if (order_count < 5) {
            ImGui_Separator();
        }
    }

    sqlite3_finalize(statement);

    if (order_count == 0) {
        ImGui_TextDisabled("No orders yet.");
        ImGui_TextWrapped("Completed purchases will appear here once checkout saves orders to the database.");
    }
}

static void draw_logged_in_account(Shop* shop) {
    Account_State* account = &shop->account;

    char signed_in_text[180];
    snprintf(signed_in_text, sizeof(signed_in_text), "Signed in as %s", account->current_username);
    center_account_text(signed_in_text);

    ImGui_Spacing();
    ImGui_Separator();
    ImGui_Spacing();

    ImGui_TextUnformatted("Account Information");
    ImGui_TextDisabled("Username");
    ImGui_Text("%s", account->current_username);

    if (account->current_email[0] == '\0') {
        ImGui_TextWrapped("Recovery reminder: add an email so Forgot Password can work later.");
    }

    ImGui_TextDisabled("Recovery Email");
    ImGui_SetNextItemWidth(-1.0f);
    ImGui_InputTextWithHint("##account_email_settings", "Email address",
                            account->email, sizeof(account->email), 0);

    if (ImGui_ButtonEx("Save Email##account_save_email", (ImVec2){-1.0f, 38.0f})) {
        update_recovery_email(shop, account->email,
                              account->settings_message, sizeof(account->settings_message));
    }

    ImGui_Spacing();
    ImGui_Separator();
    ImGui_Spacing();

    ImGui_TextUnformatted("Change Password");
    ImGuiInputTextFlags password_flags = account->show_password ? 0 : ImGuiInputTextFlags_Password;

    ImGui_SetNextItemWidth(-1.0f);
    ImGui_InputTextWithHint("##account_current_password", "Current password",
                            account->current_password, sizeof(account->current_password), password_flags);

    ImGui_SetNextItemWidth(-1.0f);
    ImGui_InputTextWithHint("##account_new_password", "New password",
                            account->new_password, sizeof(account->new_password), password_flags);

    ImGui_SetNextItemWidth(-1.0f);
    ImGui_InputTextWithHint("##account_confirm_password", "Confirm new password",
                            account->confirm_password, sizeof(account->confirm_password), password_flags);

    if (ImGui_ButtonEx(account->show_password ?
                       "Hide Passwords##account_show_change_password" :
                       "Show Passwords##account_show_change_password",
                       (ImVec2){180.0f, 34.0f})) {
        account->show_password = !account->show_password;
    }

    ImGui_SameLine();
    if (ImGui_ButtonEx("Change Password##account_change_password", (ImVec2){-1.0f, 34.0f})) {
        bool password_changed = change_account_password(
            shop,
            account->current_password,
            account->new_password,
            account->confirm_password,
            account->settings_message,
            sizeof(account->settings_message));

        if (password_changed) {
            account->current_password[0] = '\0';
            account->new_password[0] = '\0';
            account->confirm_password[0] = '\0';
        }
    }

    if (account->settings_message[0] != '\0') {
        ImGui_TextWrapped("%s", account->settings_message);
    }

    ImGui_Spacing();
    ImGui_Separator();
    ImGui_Spacing();

    ImGui_TextUnformatted("Order History");
    draw_order_history(shop);

    ImGui_Spacing();
    if (ImGui_ButtonEx("Logout##account_logout", (ImVec2){-1.0f, 38.0f})) {
        auth_logout(shop);
    }

    if (ImGui_ButtonEx("Back to Shop##account_back_logged_in", (ImVec2){-1.0f, 36.0f})) {
        screen_swap(shop, HOME_SCREEN);
    }
}

static void draw_logged_out_account(Shop* shop) {
    Account_State* account = &shop->account;

    if (account->forgot_password_mode) {
        center_account_text("FORGOT PASSWORD");
        ImGui_Spacing();
        ImGui_TextWrapped("Enter the recovery email saved on the account. This local version only simulates the email request.");

        ImGui_SetNextItemWidth(-1.0f);
        ImGui_InputTextWithHint("##forgot_password_email", "Recovery email",
                                account->email, sizeof(account->email), 0);

        if (ImGui_ButtonEx("Send Reset Email##forgot_send_reset", (ImVec2){-1.0f, 40.0f})) {
            send_demo_password_reset(shop, account->email,
                                     account->message, sizeof(account->message));
        }

        if (account->message[0] != '\0') {
            ImGui_TextWrapped("%s", account->message);
        }

        if (ImGui_ButtonEx("Back to Login##forgot_back_login", (ImVec2){-1.0f, 36.0f})) {
            account->forgot_password_mode = false;
            account->message[0] = '\0';
            account->email[0] = '\0';
        }
        return;
    }

    if (ImGui_ButtonEx("Login##account_tab_login", (ImVec2){205.0f, 38.0f})) {
        account->register_mode = false;
        account->message[0] = '\0';
    }

    ImGui_SameLine();
    if (ImGui_ButtonEx("Register##account_tab_register", (ImVec2){205.0f, 38.0f})) {
        account->register_mode = true;
        account->message[0] = '\0';
    }

    ImGui_Spacing();
    ImGui_TextDisabled("Username");
    ImGui_SetNextItemWidth(-1.0f);
    ImGui_InputTextWithHint("##account_username", "Username",
                            account->username, sizeof(account->username), 0);

    ImGui_TextDisabled("Password");
    ImGui_SetNextItemWidth(-52.0f);
    ImGuiInputTextFlags password_flags = account->show_password ? 0 : ImGuiInputTextFlags_Password;
    ImGui_InputTextWithHint("##account_password", "Password",
                            account->password, sizeof(account->password), password_flags);
    draw_password_visibility_button(account, "##account_password_eye");

    const char* submit_label = account->register_mode ?
        "Create Account##account_submit_register" :
        "Login##account_submit_login";

    if (ImGui_ButtonEx(submit_label, (ImVec2){-1.0f, 42.0f})) {
        submit_login_or_registration(shop);
    }

    if (!account->register_mode) {
        if (ImGui_ButtonEx("Forgot Password?##account_forgot_password", (ImVec2){-1.0f, 34.0f})) {
            account->forgot_password_mode = true;
            account->message[0] = '\0';
            account->email[0] = '\0';
        }
    } else {
        ImGui_TextWrapped("After registering, log in and add a recovery email in Account Settings.");
    }

    if (account->message[0] != '\0') {
        ImGui_TextWrapped("%s", account->message);
    }

    if (ImGui_ButtonEx("Back to Shop##account_back_logged_out", (ImVec2){-1.0f, 36.0f})) {
        screen_swap(shop, HOME_SCREEN);
    }
}

void account_panel(Shop* shop) {
    Account_State* account = &shop->account;

    float panel_width = account->logged_in ? 620.0f : 500.0f;
    float panel_height = account->logged_in ? 700.0f : 560.0f;

    ImGuiIO* io = ImGui_GetIO();
    ImVec2 center = {io->DisplaySize.x * 0.5f, io->DisplaySize.y * 0.5f};

    ImGui_SetNextWindowPosEx(center, ImGuiCond_Always, (ImVec2){0.5f, 0.5f});
    ImGui_SetNextWindowSize((ImVec2){panel_width, panel_height}, ImGuiCond_Always);

    push_account_theme();

    ImGuiWindowFlags window_flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings;

    if (ImGui_Begin("##chair_shop_account", NULL, window_flags)) {
        center_account_text(account->logged_in ? "ACCOUNT SETTINGS" : "WELCOME TO CHAIR SHOP");
        ImGui_Spacing();

        if (account->logged_in) {
            draw_logged_in_account(shop);
        } else {
            draw_logged_out_account(shop);
        }
    }

    ImGui_End();
    pop_account_theme();
}
