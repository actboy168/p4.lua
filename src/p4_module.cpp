#include <lua.hpp>

#if defined(_WIN32)
#define P4LUA_EXPORT extern "C" __declspec(dllexport)
#else
#define P4LUA_EXPORT extern "C" __attribute__((visibility("default")))
#endif

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "p4/clientapi.h"

namespace {

constexpr char kClientMetatable[] = "p4.client";
struct InfoRecord {
    int level = 0;
    std::string data;
};

struct CommandResult {
    bool ok = true;
    int error_count = 0;
    std::vector<InfoRecord> info;
    std::vector<std::string> text;
    std::vector<std::string> binary;
    std::vector<std::string> errors;
    std::vector<std::map<std::string, std::string>> stats;

    void AddError(std::string message) {
        if (!message.empty() && message.back() == '\n') {
            message.pop_back();
        }
        ok = false;
        errors.push_back(std::move(message));
    }
};

std::string ToString(const StrPtr& value) {
    return std::string(value.Text(), static_cast<size_t>(value.Length()));
}

std::string FormatError(const Error& error, int opts = EF_PLAIN) {
    StrBuf buffer;
    error.Fmt(&buffer, opts);
    std::string text(buffer.Text(), static_cast<size_t>(buffer.Length()));
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
    }
    return text;
}

class GlobalP4State {
public:
    static bool Acquire(std::string* error) {
        (void)error;
        return true;
    }

    static void Release() noexcept {
        return;
    }
};

class ThreadP4Scope {
public:
    explicit ThreadP4Scope(std::string* error) {
        (void)error;
    }

    ~ThreadP4Scope() {
    }

    bool Ok() const {
        return true;
    }
};

class ResultCollector : public ClientUser {
public:
    explicit ResultCollector(CommandResult& result)
        : result_(result) {
    }

    void HandleError(Error* error) override {
        result_.AddError(FormatError(*error));
    }

    void Message(Error* error) override {
        std::string message = FormatError(*error);
        if (error->IsError() || error->IsFatal()) {
            result_.AddError(std::move(message));
            return;
        }
        result_.info.push_back({0, std::move(message)});
    }

    void OutputError(const char* error_buffer) override {
        result_.AddError(error_buffer != nullptr ? error_buffer : "");
    }

    void OutputInfo(char level, const char* data) override {
        result_.info.push_back({
            static_cast<unsigned char>(level),
            data != nullptr ? data : "",
        });
    }

    void OutputBinary(const char* data, int length) override {
        result_.binary.emplace_back(data != nullptr ? std::string(data, static_cast<size_t>(length)) : std::string());
    }

    void OutputText(const char* data, int length) override {
        result_.text.emplace_back(data != nullptr ? std::string(data, static_cast<size_t>(length)) : std::string());
    }

    void OutputStat(StrDict* variables) override {
        std::map<std::string, std::string> entry;
        for (int index = 0;; ++index) {
            StrRef key;
            StrRef value;
            if (!variables->GetVar(index, key, value)) {
                break;
            }
            entry.emplace(ToString(key), ToString(value));
        }
        result_.stats.push_back(std::move(entry));
    }

private:
    CommandResult& result_;
};

class P4Client {
public:
    P4Client()
        : api_(std::make_unique<ClientApi>()) {
    }

    ~P4Client() {
        CloseNoexcept();
        GlobalP4State::Release();
    }

    bool Configure(const std::string& key, const std::string& value, std::string* error) {
        if (key == "port" && connected_) {
            if (error != nullptr) {
                *error = "port must be configured before connect";
            }
            return false;
        }

        ThreadP4Scope thread_scope(error);
        if (!thread_scope.Ok()) {
            return false;
        }

        if (!ApplyOption(key, value)) {
            if (error != nullptr) {
                *error = "unsupported option: " + key;
            }
            return false;
        }
        settings_[key] = value;
        return true;
    }

    bool GetOption(const std::string& key, std::string* value) const {
        const auto iterator = settings_.find(key);
        if (iterator == settings_.end()) {
            return false;
        }
        if (value != nullptr) {
            *value = iterator->second;
        }
        return true;
    }

    bool Connect(std::string* error) {
        ThreadP4Scope thread_scope(error);
        if (!thread_scope.Ok()) {
            return false;
        }
        return ConnectImpl(error);
    }

    bool Close(std::string* error) {
        ThreadP4Scope thread_scope(error);
        if (!thread_scope.Ok()) {
            return false;
        }
        return CloseImpl(error);
    }

    bool Connected() const {
        return connected_;
    }

    bool Run(const std::string& command, const std::vector<std::string>& arguments, CommandResult* result, std::string* error) {
        if (command.empty()) {
            if (error != nullptr) {
                *error = "command must not be empty";
            }
            return false;
        }

        ThreadP4Scope thread_scope(error);
        if (!thread_scope.Ok()) {
            return false;
        }

        if (connected_ && api_->Dropped()) {
            connected_ = false;
        }
        if (!connected_ && !ConnectImpl(error)) {
            return false;
        }

        std::vector<std::string> argv_storage = arguments;
        std::vector<char*> argv;
        argv.reserve(argv_storage.size());
        for (std::string& argument : argv_storage) {
            argv.push_back(argument.data());
        }

        api_->SetArgv(static_cast<int>(argv.size()), argv.empty() ? nullptr : argv.data());

        CommandResult command_result;
        ResultCollector collector(command_result);
        api_->Run(command.c_str(), &collector);
        command_result.error_count = api_->GetErrors();
        command_result.ok = command_result.errors.empty() && api_->GetErrors() == 0 && api_->GetFatals() == 0;

        if (api_->Dropped()) {
            connected_ = false;
        }

        if (result != nullptr) {
            *result = std::move(command_result);
        }
        return true;
    }

private:
    bool ApplyOption(const std::string& key, const std::string& value) {
        if (key == "charset") {
            api_->SetCharset(value.c_str());
            return true;
        }
        if (key == "client") {
            api_->SetClient(value.c_str());
            return true;
        }
        if (key == "cwd") {
            api_->SetCwd(value.c_str());
            return true;
        }
        if (key == "enviro_file") {
            api_->SetEnviroFile(value.c_str());
            return true;
        }
        if (key == "host") {
            api_->SetHost(value.c_str());
            return true;
        }
        if (key == "ignore_file") {
            api_->SetIgnoreFile(value.c_str());
            return true;
        }
        if (key == "language") {
            api_->SetLanguage(value.c_str());
            return true;
        }
        if (key == "password") {
            api_->SetPassword(value.c_str());
            return true;
        }
        if (key == "port") {
            api_->SetPort(value.c_str());
            return true;
        }
        if (key == "prog") {
            api_->SetProg(value.c_str());
            return true;
        }
        if (key == "ticket_file") {
            api_->SetTicketFile(value.c_str());
            return true;
        }
        if (key == "trust_file") {
            api_->SetTrustFile(value.c_str());
            return true;
        }
        if (key == "user") {
            api_->SetUser(value.c_str());
            return true;
        }
        if (key == "version") {
            api_->SetVersion(value.c_str());
            return true;
        }
        return false;
    }

    bool ConnectImpl(std::string* error) {
        if (connected_) {
            return true;
        }
        Error p4_error;
        api_->Init(&p4_error);
        if (p4_error.Test()) {
            if (error != nullptr) {
                *error = FormatError(p4_error);
            }
            return false;
        }
        connected_ = true;
        return true;
    }

    bool CloseImpl(std::string* error) {
        if (!connected_) {
            return true;
        }
        Error p4_error;
        api_->Final(&p4_error);
        connected_ = false;
        if (p4_error.Test()) {
            if (error != nullptr) {
                *error = FormatError(p4_error);
            }
            return false;
        }
        return true;
    }

    void CloseNoexcept() noexcept {
        std::string error;
        ThreadP4Scope thread_scope(&error);
        if (!thread_scope.Ok()) {
            return;
        }
        Error p4_error;
        if (connected_) {
            api_->Final(&p4_error);
            connected_ = false;
        }
    }

    std::unique_ptr<ClientApi> api_;
    bool connected_ = false;
    std::unordered_map<std::string, std::string> settings_;
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
    constexpr std::string_view options[] = {
        "charset",
        "client",
        "cwd",
        "enviro_file",
        "host",
        "ignore_file",
        "language",
        "password",
        "port",
        "prog",
        "ticket_file",
        "trust_file",
        "user",
        "version",
    };

    return std::find(std::begin(options), std::end(options), key) != std::end(options);
}

std::string LuaValueToString(lua_State* L, int index) {
    size_t length = 0;
    const char* value = luaL_tolstring(L, index, &length);
    std::string text(value, length);
    lua_pop(L, 1);
    return text;
}

bool ConfigureFromTable(lua_State* L, int index, P4Client* client, std::string* error) {
    index = lua_absindex(L, index);
    lua_pushnil(L);
    while (lua_next(L, index) != 0) {
        if (!lua_isstring(L, -2)) {
            lua_pop(L, 2);
            if (error != nullptr) {
                *error = "configuration keys must be strings";
            }
            return false;
        }

        size_t key_length = 0;
        const char* key = lua_tolstring(L, -2, &key_length);
        std::string_view key_view(key, key_length);
        if (!IsSupportedOption(key_view)) {
            lua_pop(L, 2);
            if (error != nullptr) {
                *error = "unsupported option: " + std::string(key_view);
            }
            return false;
        }

        std::string value = LuaValueToString(L, -1);
        if (!client->Configure(std::string(key_view), value, error)) {
            lua_pop(L, 1);
            return false;
        }
        lua_pop(L, 1);
    }
    return true;
}

std::vector<std::string> ParseArguments(lua_State* L, int index) {
    std::vector<std::string> arguments;
    if (lua_isnoneornil(L, index)) {
        return arguments;
    }

    luaL_checktype(L, index, LUA_TTABLE);
    const lua_Integer count = luaL_len(L, index);
    arguments.reserve(static_cast<size_t>(count));
    for (lua_Integer i = 1; i <= count; ++i) {
        lua_geti(L, index, i);
        arguments.push_back(LuaValueToString(L, -1));
        lua_pop(L, 1);
    }
    return arguments;
}

void PushStringArray(lua_State* L, const std::vector<std::string>& values) {
    lua_createtable(L, static_cast<int>(values.size()), 0);
    int array_index = 1;
    for (const std::string& value : values) {
        lua_pushlstring(L, value.data(), value.size());
        lua_seti(L, -2, array_index++);
    }
}

void PushInfoArray(lua_State* L, const std::vector<InfoRecord>& values) {
    lua_createtable(L, static_cast<int>(values.size()), 0);
    int array_index = 1;
    for (const InfoRecord& value : values) {
        lua_createtable(L, 0, 2);
        lua_pushinteger(L, value.level);
        lua_setfield(L, -2, "level");
        lua_pushlstring(L, value.data.data(), value.data.size());
        lua_setfield(L, -2, "data");
        lua_seti(L, -2, array_index++);
    }
}

void PushStatsArray(lua_State* L, const std::vector<std::map<std::string, std::string>>& values) {
    lua_createtable(L, static_cast<int>(values.size()), 0);
    int array_index = 1;
    for (const auto& item : values) {
        lua_createtable(L, 0, static_cast<int>(item.size()));
        for (const auto& pair : item) {
            lua_pushlstring(L, pair.second.data(), pair.second.size());
            lua_setfield(L, -2, pair.first.c_str());
        }
        lua_seti(L, -2, array_index++);
    }
}

int PushBooleanOrError(lua_State* L, bool ok, const std::string& error) {
    if (!ok) {
        lua_pushnil(L);
        lua_pushlstring(L, error.data(), error.size());
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}

void PushCommandResult(lua_State* L, const CommandResult& result) {
    lua_createtable(L, 0, 6);

    lua_pushboolean(L, result.ok);
    lua_setfield(L, -2, "ok");

    lua_pushinteger(L, result.error_count);
    lua_setfield(L, -2, "error_count");

    PushInfoArray(L, result.info);
    lua_setfield(L, -2, "info");

    PushStringArray(L, result.text);
    lua_setfield(L, -2, "text");

    PushStringArray(L, result.binary);
    lua_setfield(L, -2, "binary");

    PushStringArray(L, result.errors);
    lua_setfield(L, -2, "errors");

    PushStatsArray(L, result.stats);
    lua_setfield(L, -2, "stats");
}

int ClientConfigure(lua_State* L) {
    P4Client* client = CheckClient(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);

    std::string error;
    if (!ConfigureFromTable(L, 2, client, &error)) {
        lua_pushnil(L);
        lua_pushlstring(L, error.data(), error.size());
        return 2;
    }

    lua_settop(L, 1);
    return 1;
}

int ClientSet(lua_State* L) {
    P4Client* client = CheckClient(L, 1);
    const char* key = luaL_checkstring(L, 2);
    luaL_argcheck(L, IsSupportedOption(key), 2, "unsupported option");
    std::string value = LuaValueToString(L, 3);

    std::string error;
    if (!client->Configure(key, value, &error)) {
        lua_pushnil(L);
        lua_pushlstring(L, error.data(), error.size());
        return 2;
    }

    lua_settop(L, 1);
    return 1;
}

int ClientGet(lua_State* L) {
    P4Client* client = CheckClient(L, 1);
    const char* key = luaL_checkstring(L, 2);
    luaL_argcheck(L, IsSupportedOption(key), 2, "unsupported option");

    std::string value;
    if (!client->GetOption(key, &value)) {
        lua_pushnil(L);
        return 1;
    }

    lua_pushlstring(L, value.data(), value.size());
    return 1;
}

int ClientAccessor(lua_State* L) {
    const char* key = lua_tostring(L, lua_upvalueindex(1));
    P4Client* client = CheckClient(L, 1);
    if (lua_gettop(L) >= 2 && !lua_isnoneornil(L, 2)) {
        std::string error;
        if (!client->Configure(key, LuaValueToString(L, 2), &error)) {
            lua_pushnil(L);
            lua_pushlstring(L, error.data(), error.size());
            return 2;
        }
        lua_settop(L, 1);
        return 1;
    }

    std::string value;
    if (!client->GetOption(key, &value)) {
        lua_pushnil(L);
        return 1;
    }

    lua_pushlstring(L, value.data(), value.size());
    return 1;
}

int ClientConnect(lua_State* L) {
    P4Client* client = CheckClient(L, 1);
    std::string error;
    return PushBooleanOrError(L, client->Connect(&error), error);
}

int ClientClose(lua_State* L) {
    P4Client* client = CheckClient(L, 1);
    std::string error;
    return PushBooleanOrError(L, client->Close(&error), error);
}

int ClientConnected(lua_State* L) {
    P4Client* client = CheckClient(L, 1);
    lua_pushboolean(L, client->Connected());
    return 1;
}

int ClientRun(lua_State* L) {
    P4Client* client = CheckClient(L, 1);
    const char* command = luaL_checkstring(L, 2);
    std::vector<std::string> arguments = ParseArguments(L, 3);

    CommandResult result;
    std::string error;
    if (!client->Run(command, arguments, &result, &error)) {
        lua_pushnil(L);
        lua_pushlstring(L, error.data(), error.size());
        return 2;
    }

    PushCommandResult(L, result);
    return 1;
}

int ClientGc(lua_State* L) {
    auto* userdata = static_cast<P4ClientUserData*>(luaL_checkudata(L, 1, kClientMetatable));
    delete userdata->client;
    userdata->client = nullptr;
    return 0;
}

int ModuleNew(lua_State* L) {
    std::string error;
    if (!GlobalP4State::Acquire(&error)) {
        return luaL_error(L, "%s", error.c_str());
    }

    auto* userdata = static_cast<P4ClientUserData*>(lua_newuserdatauv(L, sizeof(P4ClientUserData), 0));
    userdata->client = nullptr;

    try {
        userdata->client = new P4Client();
    } catch (...) {
        GlobalP4State::Release();
        return luaL_error(L, "failed to allocate p4 client");
    }

    luaL_getmetatable(L, kClientMetatable);
    lua_setmetatable(L, -2);

    if (!lua_isnoneornil(L, 1)) {
        luaL_checktype(L, 1, LUA_TTABLE);
        if (!ConfigureFromTable(L, 1, userdata->client, &error)) {
            delete userdata->client;
            userdata->client = nullptr;
            GlobalP4State::Release();
            lua_pushnil(L);
            lua_pushlstring(L, error.data(), error.size());
            return 2;
        }
    }

    return 1;
}

void RegisterAccessor(lua_State* L, const char* key) {
    lua_pushstring(L, key);
    lua_pushcclosure(L, ClientAccessor, 1);
    lua_setfield(L, -2, key);
}

} // namespace

P4LUA_EXPORT int luaopen_p4(lua_State* L) {
    luaL_newmetatable(L, kClientMetatable);

    lua_pushcfunction(L, ClientGc);
    lua_setfield(L, -2, "__gc");

    lua_newtable(L);

    luaL_Reg methods[] = {
        {"close", ClientClose},
        {"configure", ClientConfigure},
        {"connect", ClientConnect},
        {"connected", ClientConnected},
        {"get", ClientGet},
        {"run", ClientRun},
        {"set", ClientSet},
        {nullptr, nullptr},
    };
    luaL_setfuncs(L, methods, 0);

    RegisterAccessor(L, "charset");
    RegisterAccessor(L, "client");
    RegisterAccessor(L, "cwd");
    RegisterAccessor(L, "enviro_file");
    RegisterAccessor(L, "host");
    RegisterAccessor(L, "ignore_file");
    RegisterAccessor(L, "language");
    RegisterAccessor(L, "password");
    RegisterAccessor(L, "port");
    RegisterAccessor(L, "prog");
    RegisterAccessor(L, "ticket_file");
    RegisterAccessor(L, "trust_file");
    RegisterAccessor(L, "user");
    RegisterAccessor(L, "version");

    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);

    lua_newtable(L);

    lua_pushcfunction(L, ModuleNew);
    lua_setfield(L, -2, "new");

    lua_pushliteral(L, "0.1.0");
    lua_setfield(L, -2, "_VERSION");

    return 1;
}
