#include <lua.hpp>

#if defined(_WIN32)
#define P4LUA_EXPORT extern "C" __declspec(dllexport)
#else
#define P4LUA_EXPORT extern "C" __attribute__((visibility("default")))
#endif

#include <string_view>

#include "p4/clientapi.h"
#include "luaref.h"

namespace {

constexpr char kClientMetatable[] = "p4.client";
StrBuf FormatError(const Error& error, int opts = EF_PLAIN) {
    StrBuf buffer;
    error.Fmt(&buffer, opts);
    int len = buffer.Length();
    const char* text = buffer.Text();
    while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r')) {
        --len;
    }
    buffer.SetLength(len);
    return buffer;
}

class ResultCollector : public ClientUser {
public:
    explicit ResultCollector(lua_State* L, luaref refMgr, int options_ref = -1)
        : L_(L), refMgr_(refMgr), options_ref_(options_ref) {
        lua_newtable(L);
        ref_ = luaref_ref(refMgr_, L);
    }

    ~ResultCollector() override {
        if (ref_ != -1) {
            luaref_unref(refMgr_, ref_);
        }
    }

    int Release() {
        int ref = ref_;
        ref_ = -1;
        luaref_get(refMgr_, L_, ref);
        luaref_unref(refMgr_, ref);
        return 1;
    }

    void HandleError(Error* error) override {
        auto buf = FormatError(*error);
        EnsureField("errors");
        PushError(error->GetGeneric(), error->GetSeverity(), buf.Text(), static_cast<size_t>(buf.Length()));
        Append();
    }

    void Message(Error* error) override {
        auto buf = FormatError(*error);
        if (error->IsInfo()) {
            EnsureField("info");
            PushInfo(0, buf.Text(), static_cast<size_t>(buf.Length()));
            Append();
        } else if (error->IsWarning()) {
            EnsureField("warnings");
            PushError(error->GetGeneric(), 2, buf.Text(), static_cast<size_t>(buf.Length()));
            Append();
        } else {
            EnsureField("errors");
            PushError(error->GetGeneric(), error->GetSeverity(), buf.Text(), static_cast<size_t>(buf.Length()));
            Append();
        }
    }

    void OutputError(const char* error_buffer) override {
        // HandleError/Message cover all error paths; OutputError is not called.
    }

    void OutputInfo(char level, const char* data) override {
        EnsureField("info");
        const char* s = data != nullptr ? data : "";
        PushInfo(static_cast<int>(level), s, strlen(s));
        Append();
    }

    void OutputBinary(const char* data, int length) override {
        EnsureField("binary");
        lua_pushlstring(L_, data != nullptr ? data : "", data != nullptr ? static_cast<size_t>(length) : 0);
        Append();
    }

    void OutputText(const char* data, int length) override {
        EnsureField("text");
        lua_pushlstring(L_, data != nullptr ? data : "", data != nullptr ? static_cast<size_t>(length) : 0);
        Append();
    }

    void OutputStat(StrDict* variables) override {
        EnsureField("stats");
        lua_newtable(L_);
        for (int index = 0;; ++index) {
            StrRef key;
            StrRef value;
            if (!variables->GetVar(index, key, value)) break;
            lua_pushlstring(L_, key.Text(), static_cast<size_t>(key.Length()));
            lua_pushlstring(L_, value.Text(), static_cast<size_t>(value.Length()));
            lua_rawset(L_, -3);
        }
        Append();
    }

    void InputData(StrBuf* strbuf, Error* e) override {
        if (options_ref_ != LUA_NOREF) {
            luaref_get(refMgr_, L_, options_ref_);
            lua_getfield(L_, -1, "input");
            if (lua_isstring(L_, -1)) {
                size_t len = 0;
                const char* s = lua_tolstring(L_, -1, &len);
                strbuf->Set(s);
            }
            lua_pop(L_, 2);
        }
    }

    void Prompt(const StrPtr& msg, StrBuf& rsp, int noEcho, Error* e) override {
        if (options_ref_ != LUA_NOREF) {
            luaref_get(refMgr_, L_, options_ref_);
            lua_getfield(L_, -1, "prompt");
            if (lua_isstring(L_, -1)) {
                size_t len = 0;
                const char* s = lua_tolstring(L_, -1, &len);
                rsp.Set(s);
            }
            lua_pop(L_, 2);
        }
    }

private:
    void PushResult() {
        luaref_get(refMgr_, L_, ref_);
    }

    void EnsureField(const char* field) {
        PushResult();
        if (lua_getfield(L_, -1, field) == LUA_TNIL) {
            lua_pop(L_, 1);
            lua_newtable(L_);
            lua_pushvalue(L_, -1);
            lua_setfield(L_, -3, field);
        }
        lua_remove(L_, -2);
    }

    void Append() {
        int len = static_cast<int>(luaL_len(L_, -2));
        lua_rawseti(L_, -2, len + 1);
        lua_pop(L_, 1);
    }

    void PushError(int code, int severity, const char* msg, size_t len) {
        while (len > 0 && msg[len - 1] == '\n') --len;
        lua_createtable(L_, 0, 3);
        lua_pushinteger(L_, code); lua_setfield(L_, -2, "code");
        lua_pushinteger(L_, severity); lua_setfield(L_, -2, "severity");
        lua_pushlstring(L_, msg, len); lua_setfield(L_, -2, "message");
    }

    void PushInfo(int level, const char* msg, size_t len) {
        lua_createtable(L_, 0, 2);
        lua_pushinteger(L_, level); lua_setfield(L_, -2, "level");
        lua_pushlstring(L_, msg, len); lua_setfield(L_, -2, "data");
    }

    lua_State* L_;
    luaref refMgr_;
    int ref_ = -1;
    int options_ref_ = -1;
};

class TagCollector : public ClientUser {
public:
    TagCollector(lua_State* L, int on_stat_ref, int on_error_ref,
                 int on_warning_ref, int on_info_ref, int on_text_ref,
                 int on_binary_ref, int on_input_ref, int on_prompt_ref)
        : L_(L),
          on_stat_ref_(on_stat_ref), on_error_ref_(on_error_ref),
          on_warning_ref_(on_warning_ref), on_info_ref_(on_info_ref),
          on_text_ref_(on_text_ref), on_binary_ref_(on_binary_ref),
          on_input_ref_(on_input_ref), on_prompt_ref_(on_prompt_ref) {
    }

    ~TagCollector() override {}

    int Release(bool ok) {
        lua_pushboolean(L_, ok);
        return 1;
    }

    void HandleError(Error* error) override {
        if (on_error_ref_ == LUA_NOREF) return;
        int code = error->GetGeneric();
        int severity = error->GetSeverity();
        auto buf = FormatError(*error);
        CallCallback(on_error_ref_, [&]() {
            lua_pushinteger(L_, code);
            lua_pushinteger(L_, severity);
            lua_pushlstring(L_, buf.Text(), static_cast<size_t>(buf.Length()));
        }, 3);
    }

    void Message(Error* error) override {
        auto buf = FormatError(*error);
        if (error->IsInfo()) {
            if (on_info_ref_ == LUA_NOREF) return;
            CallCallback(on_info_ref_, [&]() {
                lua_pushinteger(L_, 0);
                lua_pushlstring(L_, buf.Text(), static_cast<size_t>(buf.Length()));
            }, 2);
        } else if (error->IsWarning()) {
            if (on_warning_ref_ == LUA_NOREF) return;
            int code = error->GetGeneric();
            CallCallback(on_warning_ref_, [&]() {
                lua_pushinteger(L_, code);
                lua_pushlstring(L_, buf.Text(), static_cast<size_t>(buf.Length()));
            }, 2);
        } else {
            if (on_error_ref_ == LUA_NOREF) return;
            int code = error->GetGeneric();
            int severity = error->GetSeverity();
            CallCallback(on_error_ref_, [&]() {
                lua_pushinteger(L_, code);
                lua_pushinteger(L_, severity);
                lua_pushlstring(L_, buf.Text(), static_cast<size_t>(buf.Length()));
            }, 3);
        }
    }

    void OutputError(const char* error_buffer) override {
        // HandleError/Message cover all error paths; OutputError is not called.
    }
    void OutputInfo(char level, const char* data) override {
        if (on_info_ref_ == LUA_NOREF) return;
        const char* s = data ? data : "";
        CallCallback(on_info_ref_, [&]() {
            lua_pushinteger(L_, static_cast<int>(level));
            lua_pushlstring(L_, s, strlen(s));
        }, 2);
    }

    void OutputBinary(const char* data, int length) override {
        if (on_binary_ref_ == LUA_NOREF) return;
        CallCallback(on_binary_ref_, [&]() {
            lua_pushlstring(L_, data ? data : "", data ? static_cast<size_t>(length) : 0);
        }, 1);
    }

    void OutputText(const char* data, int length) override {
        if (on_text_ref_ == LUA_NOREF) return;
        CallCallback(on_text_ref_, [&]() {
            lua_pushlstring(L_, data ? data : "", data ? static_cast<size_t>(length) : 0);
        }, 1);
    }

    void OutputStat(StrDict* variables) override {
        if (on_stat_ref_ == LUA_NOREF) return;
        lua_newtable(L_);
        for (int index = 0;; ++index) {
            StrRef key;
            StrRef value;
            if (!variables->GetVar(index, key, value)) break;
            lua_pushlstring(L_, key.Text(), static_cast<size_t>(key.Length()));
            lua_pushlstring(L_, value.Text(), static_cast<size_t>(value.Length()));
            lua_rawset(L_, -3);
        }
        CallCallback(on_stat_ref_, [&]() {}, 1);
    }

    void InputData(StrBuf* strbuf, Error* e) override {
        if (on_input_ref_ == LUA_NOREF) return;
        CallCallback(on_input_ref_, []() {}, 0, 1);
        if (lua_isstring(L_, -1)) {
            size_t len = 0;
            const char* s = lua_tolstring(L_, -1, &len);
            strbuf->Set(s);
        }
        lua_pop(L_, 1);
    }

    void Prompt(const StrPtr& msg, StrBuf& rsp, int noEcho, Error* e) override {
        if (on_prompt_ref_ == LUA_NOREF) return;
        lua_pushlstring(L_, msg.Text(), static_cast<size_t>(msg.Length()));
        lua_pushboolean(L_, noEcho);
        CallCallback(on_prompt_ref_, []() {}, 2, 1);
        if (lua_isstring(L_, -1)) {
            size_t len = 0;
            const char* s = lua_tolstring(L_, -1, &len);
            rsp.Set(s);
        }
        lua_pop(L_, 1);
    }

private:
    template <typename F>
    void CallCallback(int ref, F push_args, int nargs, int nresults = 0) {
        push_args();
        int base = lua_gettop(L_) - nargs;
        lua_rawgeti(L_, LUA_REGISTRYINDEX, ref);
        lua_insert(L_, base + 1);
        if (lua_pcall(L_, nargs, nresults, 0) != LUA_OK) {
            lua_pop(L_, 1);
        }
    }

    lua_State* L_;
    int ok_ref_ = LUA_NOREF;
    int on_stat_ref_ = LUA_NOREF;
    int on_error_ref_ = LUA_NOREF;
    int on_warning_ref_ = LUA_NOREF;
    int on_info_ref_ = LUA_NOREF;
    int on_text_ref_ = LUA_NOREF;
    int on_binary_ref_ = LUA_NOREF;
    int on_input_ref_ = LUA_NOREF;
    int on_prompt_ref_ = LUA_NOREF;
};

struct OptionDef {
    const char* name;
    void (ClientApi::*set)(const char*);
};

constexpr OptionDef kOptions[] = {
    {"charset",     &ClientApi::SetCharset},
    {"client",      &ClientApi::SetClient},
    {"cwd",         &ClientApi::SetCwd},
    {"enviro_file", &ClientApi::SetEnviroFile},
    {"host",        &ClientApi::SetHost},
    {"ignore_file", &ClientApi::SetIgnoreFile},
    {"language",    &ClientApi::SetLanguage},
    {"password",    &ClientApi::SetPassword},
    {"port",        &ClientApi::SetPort},
    {"prog",        &ClientApi::SetProg},
    {"ticket_file", &ClientApi::SetTicketFile},
    {"trust_file",  &ClientApi::SetTrustFile},
    {"user",        &ClientApi::SetUser},
    {"version",     &ClientApi::SetVersion},
};

class P4Client {
public:
    explicit P4Client(lua_State* L)
        : refMgr_(luaref_init(L)), L_(L) {
        lua_newtable(L);
        settingsRef_ = luaref_ref(refMgr_, L);
    }

    ~P4Client() {
        CloseNoexcept();
        if (refMgr_) {
            luaref_close(refMgr_);
        }
    }

    bool Configure(std::string_view key, std::string_view value, lua_State* L) {
        if (key == "port" && connected_) {
            lua_pushnil(L);
            lua_pushliteral(L, "port must be configured before connect");
            return false;
        }

        if (!ApplyOption(key, value)) {
            lua_pushnil(L);
            lua_pushfstring(L, "unsupported option: %.*s", static_cast<int>(key.size()), key.data());
            return false;
        }
        luaref_get(refMgr_, L_, settingsRef_);
        lua_pushlstring(L_, key.data(), key.size());
        lua_pushlstring(L_, value.data(), value.size());
        lua_rawset(L_, -3);
        lua_pop(L_, 1);
        return true;
    }

    bool PushOption(lua_State* L, std::string_view key) const {
        luaref_get(refMgr_, L, settingsRef_);
        lua_pushlstring(L, key.data(), key.size());
        lua_rawget(L, -2);
        lua_remove(L, -2);
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            return false;
        }
        return true;
    }

    bool Connect(lua_State* L) {
        return ConnectImpl(L);
    }

    bool Close(lua_State* L) {
        return CloseImpl(L);
    }

    bool Connected() const {
        return connected_;
    }

    luaref GetRefMgr() const {
        return refMgr_;
    }

    TagCollector* RunAsync(std::string_view command, char** argv, int argc, lua_State* L,
                           int on_stat_ref, int on_error_ref, int on_warning_ref,
                           int on_info_ref, int on_text_ref, int on_binary_ref,
                           int on_input_ref, int on_prompt_ref) {
        if (connected_ && api_.Dropped()) connected_ = false;
        if (!connected_ && !ConnectImpl(L)) return nullptr;

        api_.SetArgv(argc, argv);
        auto* collector = new TagCollector(L,
            on_stat_ref, on_error_ref, on_warning_ref,
            on_info_ref, on_text_ref, on_binary_ref,
            on_input_ref, on_prompt_ref);
        api_.RunTag(command.data(), collector);
        return collector;
    }

    bool WaitTag(TagCollector* collector) {
        api_.WaitTag(collector);
        if (api_.Dropped()) connected_ = false;
        return api_.GetErrors() == 0 && api_.GetFatals() == 0;
    }

    bool Run(std::string_view command, char** argv, int argc, lua_State* L, int options_ref = -1) {
        if (command.empty()) {
            lua_pushnil(L);
            lua_pushliteral(L, "command must not be empty");
            return false;
        }

        if (connected_ && api_.Dropped()) {
            connected_ = false;
        }
        if (!connected_ && !ConnectImpl(L)) {
            return false;
        }

        api_.SetArgv(argc, argv);

        ResultCollector collector(L, refMgr_, options_ref);
        api_.Run(command.data(), &collector);

        if (api_.Dropped()) {
            connected_ = false;
        }

        collector.Release();
        return true;
    }

private:
    static const OptionDef* FindOption(std::string_view key) {
        for (const auto& opt : kOptions) {
            if (opt.name == key) return &opt;
        }
        return nullptr;
    }

    bool ApplyOption(std::string_view key, std::string_view value) {
        if (auto* opt = FindOption(key)) {
            (api_.*opt->set)(value.data());
            return true;
        }
        return false;
    }

    bool ConnectImpl(lua_State* L) {
        if (connected_) {
            return true;
        }
        Error p4_error;
        api_.SetProtocol("tag", "");
        api_.Init(&p4_error);
        if (p4_error.Test()) {
            if (L) {
                auto buf = FormatError(p4_error);
                lua_pushnil(L);
                lua_pushlstring(L, buf.Text(), static_cast<size_t>(buf.Length()));
            }
            return false;
        }
        connected_ = true;
        return true;
    }

    bool CloseImpl(lua_State* L) {
        if (!connected_) {
            return true;
        }
        Error p4_error;
        api_.Final(&p4_error);
        connected_ = false;
        if (p4_error.Test()) {
            if (L) {
                auto buf = FormatError(p4_error);
                lua_pushnil(L);
                lua_pushlstring(L, buf.Text(), static_cast<size_t>(buf.Length()));
            }
            return false;
        }
        return true;
    }

    void CloseNoexcept() noexcept {
        CloseImpl(nullptr);
    }

    ClientApi api_;
    lua_State* L_;
    luaref refMgr_;
    int settingsRef_;
    bool connected_ = false;
};

constexpr char kTagMetatable[] = "p4.tag";

struct P4ClientUserData {
    P4Client* client = nullptr;
};

struct TagUserData {
    TagCollector* collector = nullptr;
    P4Client* client = nullptr;
};

P4Client* CheckClient(lua_State* L, int index) {
    auto* userdata = static_cast<P4ClientUserData*>(luaL_checkudata(L, index, kClientMetatable));
    luaL_argcheck(L, userdata->client != nullptr, index, "p4 client is closed");
    return userdata->client;
}

bool IsSupportedOption(std::string_view key) {
    for (const auto& opt : kOptions) {
        if (opt.name == key) return true;
    }
    return false;
}

bool ConfigureFromTable(lua_State* L, int index, P4Client* client) {
    index = lua_absindex(L, index);
    lua_pushnil(L);
    while (lua_next(L, index) != 0) {
        if (!lua_isstring(L, -2)) {
            lua_pop(L, 2);
            lua_pushnil(L);
            lua_pushliteral(L, "configuration keys must be strings");
            return false;
        }

        size_t key_length = 0;
        const char* key = lua_tolstring(L, -2, &key_length);
        std::string_view key_view(key, key_length);
        if (!IsSupportedOption(key_view)) {
            lua_pop(L, 2);
            lua_pushnil(L);
            lua_pushfstring(L, "unsupported option: %.*s", static_cast<int>(key_view.size()), key_view.data());
            return false;
        }

        size_t val_len = 0;
        const char* val = lua_tolstring(L, -1, &val_len);
        if (val == nullptr) {
            const char* valtype = luaL_typename(L, -1);
            lua_pop(L, 2);
            lua_pushnil(L);
            lua_pushfstring(L, "option '%.*s' must be a string, got %s",
                            static_cast<int>(key_view.size()), key_view.data(),
                            valtype);
            return false;
        }
        if (!client->Configure(key_view, {val, val_len}, L)) {
            lua_insert(L, -4);  // move error below k:  [table, error, k, v, nil]
            lua_insert(L, -4);  // move nil below error: [table, nil, error, k, v]
            lua_pop(L, 2);      // pop v, k:               [table, nil, error]
            return false;
        }
        lua_pop(L, 1);
    }
    return true;
}

char** ParseArguments(lua_State* L, int index, int* argc) {
    if (lua_isnoneornil(L, index)) {
        *argc = 0;
        return nullptr;
    }

    luaL_checktype(L, index, LUA_TTABLE);
    const lua_Integer count = luaL_len(L, index);
    auto argv = new char*[static_cast<size_t>(count)];
    for (lua_Integer i = 0; i < count; ++i) {
        lua_geti(L, index, i + 1);
        size_t len = 0;
        const char* s = lua_tolstring(L, -1, &len);
        if (s == nullptr) {
            const char* argtype = luaL_typename(L, -1);
            delete[] argv;
            lua_pop(L, 1);
            luaL_error(L, "argument %d must be a string, got %s",
                       static_cast<int>(i + 1), argtype);
            return nullptr;  // unreachable, luaL_error never returns
        }
        argv[i] = const_cast<char*>(s);
        lua_pop(L, 1);
    }
    *argc = static_cast<int>(count);
    return argv;
}

int ClientIndex(lua_State* L) {
    P4Client* client = CheckClient(L, 1);
    const char* key = luaL_checkstring(L, 2);

    if (IsSupportedOption(key)) {
        if (!client->PushOption(L, key)) {
            lua_pushnil(L);
        }
        return 1;
    }

    lua_pushvalue(L, lua_upvalueindex(1));
    lua_pushvalue(L, 2);
    lua_gettable(L, -2);
    return 1;
}

int ClientNewIndex(lua_State* L) {
    P4Client* client = CheckClient(L, 1);
    const char* key = luaL_checkstring(L, 2);

    if (IsSupportedOption(key)) {
        size_t val_len = 0;
        const char* val = lua_tolstring(L, 3, &val_len);
        if (val == nullptr) {
            return luaL_error(L, "option '%s' must be a string, got %s",
                              key, luaL_typename(L, 3));
        }
        if (!client->Configure(key, {val, val_len}, L)) {
            return 2;
        }
        return 0;
    }

    return luaL_error(L, "unsupported option: %s", key);
}

int ClientConnect(lua_State* L) {
    P4Client* client = CheckClient(L, 1);
    if (!client->Connect(L)) {
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}

int ClientClose(lua_State* L) {
    P4Client* client = CheckClient(L, 1);
    if (!client->Close(L)) {
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}

int ClientConnected(lua_State* L) {
    P4Client* client = CheckClient(L, 1);
    lua_pushboolean(L, client->Connected());
    return 1;
}

int ClientRun(lua_State* L) {
    P4Client* client = CheckClient(L, 1);
    const char* command = luaL_checkstring(L, 2);

    int argc = 0;
    char** argv = ParseArguments(L, 3, &argc);

    int options_ref = -1;
    if (!lua_isnoneornil(L, 4)) {
        luaL_checktype(L, 4, LUA_TTABLE);
        lua_pushvalue(L, 4);
        options_ref = luaref_ref(client->GetRefMgr(), L);
    }

    if (!client->Run(command, argv, argc, L, options_ref)) {
        delete[] argv;
        return 2;
    }

    delete[] argv;
    return 1;
}

int ClientGc(lua_State* L) {
    auto* userdata = static_cast<P4ClientUserData*>(luaL_checkudata(L, 1, kClientMetatable));
    delete userdata->client;
    userdata->client = nullptr;
    return 0;
}

TagCollector* CheckTag(lua_State* L, int index) {
    auto* ud = static_cast<TagUserData*>(luaL_checkudata(L, index, kTagMetatable));
    luaL_argcheck(L, ud->collector != nullptr, index, "tag is closed");
    return ud->collector;
}

int TagWait(lua_State* L) {
    auto* ud = static_cast<TagUserData*>(luaL_checkudata(L, 1, kTagMetatable));
    TagCollector* collector = ud->collector;
    P4Client* client = ud->client;
    if (!collector) return 0;

    bool ok = client->WaitTag(collector);
    return collector->Release(ok);
}

int TagGc(lua_State* L) {
    auto* ud = static_cast<TagUserData*>(luaL_checkudata(L, 1, kTagMetatable));
    delete ud->collector;
    ud->collector = nullptr;
    ud->client = nullptr;
    return 0;
}

int ClientRunAsync(lua_State* L) {
    P4Client* client = CheckClient(L, 1);
    const char* command = luaL_checkstring(L, 2);

    int argc = 0;
    char** argv = ParseArguments(L, 3, &argc);

    int on_stat = LUA_NOREF, on_error = LUA_NOREF, on_warn = LUA_NOREF, on_info = LUA_NOREF;
    int on_text = LUA_NOREF, on_bin = LUA_NOREF, on_input = LUA_NOREF, on_prompt = LUA_NOREF;
    if (!lua_isnoneornil(L, 4)) {
        luaL_checktype(L, 4, LUA_TTABLE);
        auto get_cb = [&](const char* name) -> int {
            lua_getfield(L, 4, name);
            if (lua_isfunction(L, -1)) {
                return luaL_ref(L, LUA_REGISTRYINDEX);
            }
            lua_pop(L, 1);
            return LUA_NOREF;
        };
        on_stat   = get_cb("on_stat");
        on_error  = get_cb("on_error");
        on_warn   = get_cb("on_warning");
        on_info   = get_cb("on_info");
        on_text   = get_cb("on_text");
        on_bin    = get_cb("on_binary");
        on_input  = get_cb("on_input");
        on_prompt = get_cb("on_prompt");
    }

    auto* collector = client->RunAsync(command, argv, argc, L,
        on_stat, on_error, on_warn, on_info, on_text, on_bin, on_input, on_prompt);
    delete[] argv;

    if (!collector) {
        lua_pushnil(L);
        lua_pushliteral(L, "failed to start async command");
        return 2;
    }

    auto* ud = static_cast<TagUserData*>(lua_newuserdatauv(L, sizeof(TagUserData), 0));
    ud->collector = collector;
    ud->client = client;
    if (luaL_newmetatable(L, kTagMetatable)) {
        lua_pushcfunction(L, TagGc);
        lua_setfield(L, -2, "__gc");
        luaL_Reg methods[] = {
            {"wait", TagWait},
            {nullptr, nullptr},
        };
        luaL_newlib(L, methods);
        lua_setfield(L, -2, "__index");
    }
    lua_setmetatable(L, -2);
    return 1;
}

int ModuleNew(lua_State* L) {
    auto* userdata = static_cast<P4ClientUserData*>(lua_newuserdatauv(L, sizeof(P4ClientUserData), 0));
    userdata->client = nullptr;

    try {
        userdata->client = new P4Client(L);
    } catch (...) {
        return luaL_error(L, "failed to allocate p4 client");
    }

    if (luaL_newmetatable(L, kClientMetatable)) {
        lua_pushcfunction(L, ClientGc);
        lua_setfield(L, -2, "__gc");

        luaL_Reg methods[] = {
            {"close", ClientClose},
            {"connect", ClientConnect},
            {"connected", ClientConnected},
            {"run", ClientRun},
            {"run_async", ClientRunAsync},
            {nullptr, nullptr},
        };
        luaL_newlib(L, methods);

        lua_pushvalue(L, -1);
        lua_pushcclosure(L, ClientIndex, 1);
        lua_setfield(L, -3, "__index");

        lua_pushcfunction(L, ClientNewIndex);
        lua_setfield(L, -3, "__newindex");

        lua_pop(L, 1);
    }
    lua_setmetatable(L, -2);

    if (!lua_isnoneornil(L, 1)) {
        luaL_checktype(L, 1, LUA_TTABLE);
        if (!ConfigureFromTable(L, 1, userdata->client)) {
            delete userdata->client;
            userdata->client = nullptr;
            return 2;
        }
    }

    return 1;
}

} // namespace

P4LUA_EXPORT int luaopen_p4(lua_State* L) {
    luaL_Reg libs[] = {
        {"new", ModuleNew},
        {nullptr, nullptr},
    };
    luaL_newlib(L, libs);
    return 1;
}
