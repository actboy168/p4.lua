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
    explicit ResultCollector(lua_State* L, luaref refMgr, int input_ref = -1)
        : L_(L), refMgr_(refMgr), input_ref_(input_ref) {
        lua_createtable(L, 0, 8);
        lua_pushboolean(L, 1); lua_setfield(L, -2, "ok");
        lua_pushinteger(L, 0); lua_setfield(L, -2, "error_count");
        lua_newtable(L); lua_setfield(L, -2, "info");
        lua_newtable(L); lua_setfield(L, -2, "text");
        lua_newtable(L); lua_setfield(L, -2, "binary");
        lua_newtable(L); lua_setfield(L, -2, "errors");
        lua_newtable(L); lua_setfield(L, -2, "warnings");
        lua_newtable(L); lua_setfield(L, -2, "stats");
        ref_ = luaref_ref(refMgr_, L);
    }

    ~ResultCollector() override {
        if (ref_ != -1) {
            luaref_unref(refMgr_, ref_);
        }
    }

    void Finalize(int errors, int fatals) {
        luaref_get(refMgr_, L_, ref_);
        lua_pushboolean(L_, errors == 0 && fatals == 0);
        lua_setfield(L_, -2, "ok");
        lua_pushinteger(L_, errors);
        lua_setfield(L_, -2, "error_count");
        lua_pop(L_, 1);
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
        PushField("errors");
        PushError(error->GetGeneric(), error->GetSeverity(), buf.Text(), static_cast<size_t>(buf.Length()));
        Append();
    }

    void Message(Error* error) override {
        auto buf = FormatError(*error);
        if (error->IsInfo()) {
            PushField("info");
            PushInfo(0, buf.Text(), static_cast<size_t>(buf.Length()));
            Append();
        } else if (error->IsWarning()) {
            PushField("warnings");
            PushError(error->GetGeneric(), 2, buf.Text(), static_cast<size_t>(buf.Length()));
            Append();
        } else {
            PushField("errors");
            PushError(error->GetGeneric(), error->GetSeverity(), buf.Text(), static_cast<size_t>(buf.Length()));
            Append();
        }
    }

    void OutputError(const char* error_buffer) override {
        const char* s = error_buffer ? error_buffer : "";
        PushField("errors");
        PushError(0, 0, s, strlen(s));
        Append();
    }

    void OutputInfo(char level, const char* data) override {
        PushField("info");
        const char* s = data != nullptr ? data : "";
        PushInfo(static_cast<int>(level), s, strlen(s));
        Append();
    }

    void OutputBinary(const char* data, int length) override {
        PushField("binary");
        lua_pushlstring(L_, data != nullptr ? data : "", data != nullptr ? static_cast<size_t>(length) : 0);
        Append();
    }

    void OutputText(const char* data, int length) override {
        PushField("text");
        lua_pushlstring(L_, data != nullptr ? data : "", data != nullptr ? static_cast<size_t>(length) : 0);
        Append();
    }

    void OutputStat(StrDict* variables) override {
        PushField("stats");
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
        if (input_ref_ != -1) {
            luaref_get(refMgr_, L_, input_ref_);
            size_t len = 0;
            const char* s = lua_tolstring(L_, -1, &len);
            strbuf->Set(s);
            lua_pop(L_, 1);
        }
    }

private:
    void PushResult() {
        luaref_get(refMgr_, L_, ref_);
    }

    void PushField(const char* field) {
        PushResult();
        lua_getfield(L_, -1, field);
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
    int input_ref_ = -1;
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

    bool Run(std::string_view command, char** argv, int argc, lua_State* L, int input_ref = -1) {
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

        ResultCollector collector(L, refMgr_, input_ref);
        api_.Run(command.data(), &collector);
        collector.Finalize(api_.GetErrors(), api_.GetFatals());

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

struct P4ClientUserData {
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

    int input_ref = -1;
    if (!lua_isnoneornil(L, 4)) {
        luaL_checktype(L, 4, LUA_TTABLE);
        lua_getfield(L, 4, "input");
        if (lua_isstring(L, -1)) {
            input_ref = luaref_ref(client->GetRefMgr(), L);
        } else {
            lua_pop(L, 1);
        }
    }

    if (!client->Run(command, argv, argc, L, input_ref)) {
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
