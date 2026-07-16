#include <sqlite3.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>

namespace
{
    bool Debug_Output = false;
    std::filesystem::path Resources_Path = "./Resources";
    std::filesystem::path Storage_Path = Resources_Path / "Storage";
    std::map<std::string, sqlite3 *> Databases;
    std::string Current_Database;

    std::filesystem::path Resolve_Path(const std::string &File)
    {
        std::filesystem::path Path(File);
        if(Path.is_absolute())
        {
            return Path;
        }
        return Path;
    }

    std::filesystem::path Resolve_Database_Path(const std::string &Name)
    {
        std::filesystem::path Path(Name);
        if(Path.has_parent_path() || Path.has_extension())
        {
            return Resolve_Path(Name);
        }
        return Storage_Path / (Name + ".appealdb");
    }

    void Print(const std::string &Message)
    {
        if(Debug_Output)
        {
            std::cout << Message << std::endl;
        }
    }

    void Ensure_Parent_Directory(const std::filesystem::path &Path)
    {
        std::filesystem::path Parent = Path.parent_path();
        if(!Parent.empty())
        {
            std::filesystem::create_directories(Parent);
        }
    }

    bool Exec(sqlite3 *Database, const std::string &SQL)
    {
        char *Error = nullptr;
        int Result = sqlite3_exec(Database, SQL.c_str(), nullptr, nullptr, &Error);
        if(Result != SQLITE_OK)
        {
            std::cerr << "Storage SQLite error: " << (Error != nullptr ? Error : sqlite3_errmsg(Database)) << std::endl;
            sqlite3_free(Error);
            return false;
        }
        return true;
    }

    bool Prepare(sqlite3 *Database, const std::string &SQL, sqlite3_stmt **Statement)
    {
        int Result = sqlite3_prepare_v2(Database, SQL.c_str(), -1, Statement, nullptr);
        if(Result != SQLITE_OK)
        {
            std::cerr << "Storage SQLite prepare error: " << sqlite3_errmsg(Database) << std::endl;
            return false;
        }
        return true;
    }

    bool Initialize_Database(sqlite3 *Database)
    {
        return Exec(Database,
            "CREATE TABLE IF NOT EXISTS records ("
            "collection TEXT NOT NULL,"
            "key TEXT NOT NULL,"
            "value TEXT NOT NULL,"
            "updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "PRIMARY KEY(collection, key));"
            "CREATE INDEX IF NOT EXISTS idx_records_collection ON records(collection);");
    }

    sqlite3 *Current()
    {
        if(Current_Database.empty())
        {
            std::cerr << "Storage error: no database is open." << std::endl;
            return nullptr;
        }

        auto Found = Databases.find(Current_Database);
        if(Found == Databases.end())
        {
            std::cerr << "Storage error: current database is missing." << std::endl;
            return nullptr;
        }
        return Found->second;
    }

    void Upsert(const std::string &Collection, const std::string &Key, const std::string &Value)
    {
        sqlite3 *Database = Current();
        if(Database == nullptr)
        {
            return;
        }

        sqlite3_stmt *Statement = nullptr;
        if(!Prepare(Database,
            "INSERT INTO records(collection, key, value, updated_at) VALUES(?, ?, ?, CURRENT_TIMESTAMP) "
            "ON CONFLICT(collection, key) DO UPDATE SET value=excluded.value, updated_at=CURRENT_TIMESTAMP;",
            &Statement))
        {
            return;
        }

        sqlite3_bind_text(Statement, 1, Collection.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(Statement, 2, Key.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(Statement, 3, Value.c_str(), -1, SQLITE_TRANSIENT);

        if(sqlite3_step(Statement) != SQLITE_DONE)
        {
            std::cerr << "Storage SQLite write error: " << sqlite3_errmsg(Database) << std::endl;
        }
        else
        {
            Print("Storage set " + Collection + "." + Key + " = " + Value);
        }
        sqlite3_finalize(Statement);
    }

    std::string Escape_Field(const std::string &Text)
    {
        std::string Result;
        for(char Character : Text)
        {
            if(Character == '\\' || Character == '\t' || Character == '\n' || Character == '\r')
            {
                Result += '\\';
                if(Character == '\t') Result += 't';
                else if(Character == '\n') Result += 'n';
                else if(Character == '\r') Result += 'r';
                else Result += Character;
            }
            else
            {
                Result += Character;
            }
        }
        return Result;
    }

    std::string Unescape_Field(const std::string &Text)
    {
        std::string Result;
        for(size_t i = 0; i < Text.size(); ++i)
        {
            if(Text[i] == '\\' && i + 1 < Text.size())
            {
                ++i;
                if(Text[i] == 't') Result += '\t';
                else if(Text[i] == 'n') Result += '\n';
                else if(Text[i] == 'r') Result += '\r';
                else Result += Text[i];
            }
            else
            {
                Result += Text[i];
            }
        }
        return Result;
    }

    void Export_Collection(std::ostream &Output, const std::string &Collection)
    {
        sqlite3 *Database = Current();
        if(Database == nullptr)
        {
            return;
        }

        sqlite3_stmt *Statement = nullptr;
        std::string SQL = "SELECT collection, key, value FROM records";
        if(!Collection.empty())
        {
            SQL += " WHERE collection = ?";
        }
        SQL += " ORDER BY collection, key;";

        if(!Prepare(Database, SQL, &Statement))
        {
            return;
        }
        if(!Collection.empty())
        {
            sqlite3_bind_text(Statement, 1, Collection.c_str(), -1, SQLITE_TRANSIENT);
        }

        while(sqlite3_step(Statement) == SQLITE_ROW)
        {
            const char *Collection_Value = reinterpret_cast<const char *>(sqlite3_column_text(Statement, 0));
            const char *Key_Value = reinterpret_cast<const char *>(sqlite3_column_text(Statement, 1));
            const char *Record_Value = reinterpret_cast<const char *>(sqlite3_column_text(Statement, 2));
            Output << Escape_Field(Collection_Value != nullptr ? Collection_Value : "") << '\t'
                   << Escape_Field(Key_Value != nullptr ? Key_Value : "") << '\t'
                   << Escape_Field(Record_Value != nullptr ? Record_Value : "") << '\n';
        }

        sqlite3_finalize(Statement);
    }
}

extern "C" void Constructor()
{
    Print("Appeal_SQLite_Storage Constructor()");
}

extern "C" void Destructor()
{
    for(auto &Database : Databases)
    {
        sqlite3_close(Database.second);
    }
    Databases.clear();
    Current_Database.clear();
    Print("Appeal_SQLite_Storage Destructor()");
}

extern "C" void Debug(bool Enable)
{
    Debug_Output = Enable;
}

extern "C" void Set_Paths(std::string Resources)
{
    Resources_Path = Resolve_Path(Resources);
    Storage_Path = Resources_Path / "Storage";
    std::filesystem::create_directories(Storage_Path);
    Print("Storage path: " + Storage_Path.string());
}

extern "C" void Open(std::string Name)
{
    auto Existing = Databases.find(Name);
    if(Existing != Databases.end())
    {
        Current_Database = Name;
        Print("Storage using database: " + Name);
        return;
    }

    std::filesystem::path Database_Path = Resolve_Database_Path(Name);
    Ensure_Parent_Directory(Database_Path);

    sqlite3 *Database = nullptr;
    int Result = sqlite3_open(Database_Path.string().c_str(), &Database);
    if(Result != SQLITE_OK)
    {
        std::cerr << "Storage SQLite open error: " << (Database != nullptr ? sqlite3_errmsg(Database) : "unknown") << std::endl;
        if(Database != nullptr)
        {
            sqlite3_close(Database);
        }
        return;
    }

    if(!Initialize_Database(Database))
    {
        sqlite3_close(Database);
        return;
    }

    Databases[Name] = Database;
    Current_Database = Name;
    Print("Storage opened: " + Database_Path.string());
}

extern "C" void Close(std::string Name)
{
    auto Found = Databases.find(Name);
    if(Found == Databases.end())
    {
        return;
    }

    sqlite3_close(Found->second);
    Databases.erase(Found);
    if(Current_Database == Name)
    {
        Current_Database = Databases.empty() ? "" : Databases.begin()->first;
    }
    Print("Storage closed: " + Name);
}

extern "C" void Use(std::string Name)
{
    if(Databases.find(Name) == Databases.end())
    {
        Open(Name);
    }
    else
    {
        Current_Database = Name;
        Print("Storage using database: " + Name);
    }
}

extern "C" void Set(std::string Key, std::string Value)
{
    Upsert("Settings", Key, Value);
}

extern "C" void Get(std::string Key)
{
    sqlite3 *Database = Current();
    if(Database == nullptr)
    {
        return;
    }

    sqlite3_stmt *Statement = nullptr;
    if(!Prepare(Database, "SELECT value FROM records WHERE collection = 'Settings' AND key = ?;", &Statement))
    {
        return;
    }

    sqlite3_bind_text(Statement, 1, Key.c_str(), -1, SQLITE_TRANSIENT);
    if(sqlite3_step(Statement) == SQLITE_ROW)
    {
        const char *Value = reinterpret_cast<const char *>(sqlite3_column_text(Statement, 0));
        std::cout << (Value != nullptr ? Value : "") << std::endl;
    }
    else
    {
        Print("Storage key not found: " + Key);
    }
    sqlite3_finalize(Statement);
}

extern "C" void Insert(std::string Collection, std::string Key, std::string Value)
{
    Upsert(Collection, Key, Value);
}

extern "C" void Update(std::string Collection, std::string Key, std::string Value)
{
    Upsert(Collection, Key, Value);
}

extern "C" void Find(std::string Collection, std::string Key)
{
    sqlite3 *Database = Current();
    if(Database == nullptr)
    {
        return;
    }

    sqlite3_stmt *Statement = nullptr;
    if(!Prepare(Database, "SELECT value, updated_at FROM records WHERE collection = ? AND key = ?;", &Statement))
    {
        return;
    }

    sqlite3_bind_text(Statement, 1, Collection.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(Statement, 2, Key.c_str(), -1, SQLITE_TRANSIENT);
    if(sqlite3_step(Statement) == SQLITE_ROW)
    {
        const char *Value = reinterpret_cast<const char *>(sqlite3_column_text(Statement, 0));
        const char *Updated = reinterpret_cast<const char *>(sqlite3_column_text(Statement, 1));
        std::cout << Collection << "." << Key << " = " << (Value != nullptr ? Value : "")
                  << " [" << (Updated != nullptr ? Updated : "") << "]" << std::endl;
    }
    else
    {
        Print("Storage record not found: " + Collection + "." + Key);
    }
    sqlite3_finalize(Statement);
}

extern "C" void List(std::string Collection)
{
    sqlite3 *Database = Current();
    if(Database == nullptr)
    {
        return;
    }

    sqlite3_stmt *Statement = nullptr;
    if(!Prepare(Database, "SELECT key, value, updated_at FROM records WHERE collection = ? ORDER BY key;", &Statement))
    {
        return;
    }

    sqlite3_bind_text(Statement, 1, Collection.c_str(), -1, SQLITE_TRANSIENT);
    std::cout << "Storage collection: " << Collection << std::endl;
    while(sqlite3_step(Statement) == SQLITE_ROW)
    {
        const char *Key = reinterpret_cast<const char *>(sqlite3_column_text(Statement, 0));
        const char *Value = reinterpret_cast<const char *>(sqlite3_column_text(Statement, 1));
        const char *Updated = reinterpret_cast<const char *>(sqlite3_column_text(Statement, 2));
        std::cout << "  " << (Key != nullptr ? Key : "") << " = " << (Value != nullptr ? Value : "")
                  << " [" << (Updated != nullptr ? Updated : "") << "]" << std::endl;
    }
    sqlite3_finalize(Statement);
}

extern "C" void Remove(std::string Collection, std::string Key)
{
    sqlite3 *Database = Current();
    if(Database == nullptr)
    {
        return;
    }

    sqlite3_stmt *Statement = nullptr;
    if(!Prepare(Database, "DELETE FROM records WHERE collection = ? AND key = ?;", &Statement))
    {
        return;
    }
    sqlite3_bind_text(Statement, 1, Collection.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(Statement, 2, Key.c_str(), -1, SQLITE_TRANSIENT);
    if(sqlite3_step(Statement) != SQLITE_DONE)
    {
        std::cerr << "Storage SQLite remove error: " << sqlite3_errmsg(Database) << std::endl;
    }
    else
    {
        Print("Storage removed: " + Collection + "." + Key);
    }
    sqlite3_finalize(Statement);
}

extern "C" void Begin()
{
    sqlite3 *Database = Current();
    if(Database != nullptr)
    {
        Exec(Database, "BEGIN TRANSACTION;");
    }
}

extern "C" void Commit()
{
    sqlite3 *Database = Current();
    if(Database != nullptr)
    {
        Exec(Database, "COMMIT;");
    }
}

extern "C" void Rollback()
{
    sqlite3 *Database = Current();
    if(Database != nullptr)
    {
        Exec(Database, "ROLLBACK;");
    }
}

extern "C" void Backup(std::string File)
{
    sqlite3 *Source = Current();
    if(Source == nullptr)
    {
        return;
    }

    std::filesystem::path Backup_Path = Resolve_Path(File);
    Ensure_Parent_Directory(Backup_Path);

    sqlite3 *Destination = nullptr;
    if(sqlite3_open(Backup_Path.string().c_str(), &Destination) != SQLITE_OK)
    {
        std::cerr << "Storage SQLite backup open error: " << (Destination != nullptr ? sqlite3_errmsg(Destination) : "unknown") << std::endl;
        if(Destination != nullptr)
        {
            sqlite3_close(Destination);
        }
        return;
    }

    sqlite3_backup *Backup_Handle = sqlite3_backup_init(Destination, "main", Source, "main");
    if(Backup_Handle != nullptr)
    {
        sqlite3_backup_step(Backup_Handle, -1);
        sqlite3_backup_finish(Backup_Handle);
    }

    int Result = sqlite3_errcode(Destination);
    sqlite3_close(Destination);
    if(Result != SQLITE_OK)
    {
        std::cerr << "Storage SQLite backup error: " << Result << std::endl;
    }
    else
    {
        Print("Storage backup written: " + Backup_Path.string());
    }
}

extern "C" void Restore(std::string File)
{
    sqlite3 *Destination = Current();
    if(Destination == nullptr)
    {
        return;
    }

    sqlite3 *Source = nullptr;
    std::filesystem::path Restore_Path = Resolve_Path(File);
    if(sqlite3_open(Restore_Path.string().c_str(), &Source) != SQLITE_OK)
    {
        std::cerr << "Storage SQLite restore open error: " << (Source != nullptr ? sqlite3_errmsg(Source) : "unknown") << std::endl;
        if(Source != nullptr)
        {
            sqlite3_close(Source);
        }
        return;
    }

    sqlite3_backup *Backup_Handle = sqlite3_backup_init(Destination, "main", Source, "main");
    if(Backup_Handle != nullptr)
    {
        sqlite3_backup_step(Backup_Handle, -1);
        sqlite3_backup_finish(Backup_Handle);
    }

    int Result = sqlite3_errcode(Destination);
    sqlite3_close(Source);
    if(Result != SQLITE_OK)
    {
        std::cerr << "Storage SQLite restore error: " << Result << std::endl;
    }
    else
    {
        Print("Storage restored from: " + Restore_Path.string());
    }
}

extern "C" void Export(std::string File)
{
    std::filesystem::path Export_Path = Resolve_Path(File);
    Ensure_Parent_Directory(Export_Path);
    std::ofstream Output(Export_Path);
    if(!Output.is_open())
    {
        std::cerr << "Storage export failed: " << Export_Path.string() << std::endl;
        return;
    }

    Export_Collection(Output, "");
    Print("Storage exported: " + Export_Path.string());
}

extern "C" void Import(std::string File)
{
    std::ifstream Input(Resolve_Path(File));
    if(!Input.is_open())
    {
        std::cerr << "Storage import failed: " << File << std::endl;
        return;
    }

    std::string Line;
    while(std::getline(Input, Line))
    {
        std::stringstream Parser(Line);
        std::string Collection;
        std::string Key;
        std::string Value;
        std::getline(Parser, Collection, '\t');
        std::getline(Parser, Key, '\t');
        std::getline(Parser, Value);
        if(!Collection.empty() && !Key.empty())
        {
            Upsert(Unescape_Field(Collection), Unescape_Field(Key), Unescape_Field(Value));
        }
    }
    Print("Storage imported: " + File);
}

extern "C" void Read(std::string File)
{
    std::ifstream Input(Resolve_Path(File), std::ios::binary);
    if(!Input.is_open())
    {
        std::cerr << "Storage read failed: " << File << std::endl;
        return;
    }
    std::cout << Input.rdbuf() << std::endl;
}

extern "C" void Write(std::string Name, std::string File)
{
    std::filesystem::path Output_Path = Resolve_Path(File);
    Ensure_Parent_Directory(Output_Path);
    std::ofstream Output(Output_Path);
    if(!Output.is_open())
    {
        std::cerr << "Storage write failed: " << File << std::endl;
        return;
    }

    Export_Collection(Output, Name);
    Print("Storage collection written: " + Name + " -> " + Output_Path.string());
}

extern "C" void Exists(std::string File)
{
    bool Found = std::filesystem::exists(Resolve_Path(File));
    std::cout << (Found ? "1" : "0") << std::endl;
}

extern "C" void Delete(std::string File)
{
    std::filesystem::remove_all(Resolve_Path(File));
    Print("Storage deleted: " + File);
}

extern "C" void Save(std::string Name)
{
    Backup((Storage_Path / (Name + ".backup.appealdb")).string());
}

extern "C" void Load(std::string Name)
{
    Restore((Storage_Path / (Name + ".backup.appealdb")).string());
}
