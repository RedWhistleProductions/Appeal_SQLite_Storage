#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <memory>
#include <set>
#include <vector>

namespace Level_Storage
{
    struct Query
    {
        sqlite3 *DB;
        sqlite3_stmt *Stmt = nullptr;
        Query(const std::string &SQL) : DB(Current()) {
            if(!DB) throw std::runtime_error("Level storage requires an open database");
            if(sqlite3_prepare_v2(DB, SQL.c_str(), -1, &Stmt, nullptr) != SQLITE_OK) {
                std::string Error = sqlite3_errmsg(DB); sqlite3_finalize(Stmt);
                throw std::runtime_error(Error);
            }
        }
        Query(const Query&) = delete;
        Query &operator=(const Query&) = delete;
        ~Query() { sqlite3_finalize(Stmt); }
        void Bind(int Index, const std::string &Value) {
            if(Value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
                throw std::runtime_error("Level field exceeds SQLite size limit");
            if(sqlite3_bind_text(Stmt, Index, Value.data(), static_cast<int>(Value.size()), SQLITE_TRANSIENT) != SQLITE_OK)
                throw std::runtime_error(sqlite3_errmsg(DB));
        }
        int Step() {
            int Result = sqlite3_step(Stmt);
            if(Result != SQLITE_ROW && Result != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(DB));
            return Result;
        }
        std::string Text(int Column) const {
            const auto *Value = sqlite3_column_text(Stmt, Column);
            return Value ? std::string(reinterpret_cast<const char*>(Value), sqlite3_column_bytes(Stmt, Column)) : "";
        }
    };
    void SQL(const std::string &Text) {
        auto *DB = Current();
        if(!DB) throw std::runtime_error("Level storage requires an open database");
        char *Error = nullptr;
        if(sqlite3_exec(DB, Text.c_str(), nullptr, nullptr, &Error) != SQLITE_OK) {
            std::string Message = Error ? Error : sqlite3_errmsg(DB); sqlite3_free(Error);
            throw std::runtime_error(Message);
        }
    }
    struct Transaction {
        bool Complete = false;
        Transaction() { SQL("SAVEPOINT Appeal_Level_Write;"); }
        Transaction(const Transaction&) = delete;
        Transaction &operator=(const Transaction&) = delete;
        ~Transaction() {
            if(!Complete) sqlite3_exec(Current(), "ROLLBACK TO Appeal_Level_Write; RELEASE Appeal_Level_Write;", nullptr, nullptr, nullptr);
        }
        void Commit() { SQL("RELEASE Appeal_Level_Write;"); Complete = true; }
    };
    std::string Table(const std::string &Name, const std::string &Suffix) {
        if(Name.empty() || Name.size() > 128) throw std::runtime_error("Level name needs 1..128 UTF-8 bytes");
        const char *Hex = "0123456789ABCDEF";
        std::string Result = "Level_";
        for(unsigned char C : Name) { Result += Hex[C >> 4]; Result += Hex[C & 15]; }
        return Result + Suffix;
    }
    std::size_t Count(std::istream &In) {
        std::size_t Value = 0;
        if(!(In >> Value) || Value > 1000000) throw std::runtime_error("Invalid level record count");
        return Value;
    }
    std::string ID(const std::string &Text, bool Next = false) {
        if(Text.empty() || Text.find_first_not_of("0123456789") != std::string::npos)
            throw std::runtime_error("Invalid level entity ID");
        auto Number = std::stoull(Text);
        if(!Number || (!Next && Number == std::numeric_limits<std::uint64_t>::max()))
            throw std::runtime_error("Level entity ID outside range");
        return std::to_string(Number);
    }
    template<class T> std::string Number(T Value) {
        std::ostringstream Out; Out << std::setprecision(9) << Value; return Out.str();
    }
    struct Entity_Record { std::string Type, ID, Name, Data; int Immutable = 0; };
    struct Map_Record {
        std::string Name, Orientation, Cells;
        std::size_t Columns = 0, Rows = 0;
        std::array<float, 3> Origin{}, Size{};
        std::array<std::string, 6> Neighbors;

    };
    struct Design {
        std::string World_Name, Active, Type_Data;
        int Paused = 0; float Time_Scale = 1;
        std::map<std::string, std::string> Types;
        std::vector<Entity_Record> Entities;
        std::vector<Map_Record> Maps;
    };
    Design Parse(const std::string &Snapshot) {
        std::istringstream In(Snapshot); Design Result;
        std::string Magic; int Version = 0;
        if(!(In >> Magic >> Version >> std::quoted(Result.World_Name) >> Result.Paused >> Result.Time_Scale >> std::quoted(Result.Active)) ||
           Magic != "APPEAL_LEVEL" || Version != 1 || Result.World_Name.empty() ||
           (Result.Paused != 0 && Result.Paused != 1) || !std::isfinite(Result.Time_Scale))
            throw std::runtime_error("Invalid level snapshot header");
        for(std::size_t I = Count(In); I; --I) {
            std::string Type, Next;
            if(!(In >> std::quoted(Type) >> Next) || Type.empty() || Result.Types.count(Type))
                throw std::runtime_error("Invalid entity type list");
            Result.Types.emplace(Type, ID(Next, true));
        }
        std::ostringstream Types; Types << Result.Types.size() << '\n';
        for(const auto &Pair : Result.Types) Types << std::quoted(Pair.first) << ' ' << Pair.second << '\n';
        Result.Type_Data = Types.str();
        std::map<std::pair<std::string,std::string>, bool> Entities;
        std::set<std::string> Names;
        for(std::size_t I = Count(In); I; --I) {
            Entity_Record E;
            if(!(In >> std::quoted(E.Type) >> E.ID >> std::quoted(E.Name) >> E.Immutable >> std::quoted(E.Data)) ||
               !Result.Types.count(E.Type) || E.Name.empty() || (E.Immutable != 0 && E.Immutable != 1))
                throw std::runtime_error("Invalid level entity record");
            E.ID = ID(E.ID);
            if(std::stoull(E.ID) >= std::stoull(Result.Types.at(E.Type)) || !Names.insert(E.Name).second ||
               !Entities.emplace(std::make_pair(E.Type, E.ID), E.Immutable != 0).second)
                throw std::runtime_error("Duplicate entity or invalid next ID");
            Result.Entities.push_back(std::move(E));
        }
        std::set<std::string> Map_Names;
        std::set<std::pair<std::string,std::string>> Mutable_Tiles;
        for(std::size_t I = Count(In); I; --I) {
            Map_Record M; std::string Payload, Saved_Name;
            if(!(In >> std::quoted(M.Name) >> std::quoted(Payload)) || M.Name.empty() || !Map_Names.insert(M.Name).second)
                throw std::runtime_error("Invalid level tile-map record");
            std::istringstream Section(Payload);
            if(!(Section >> Magic >> Version >> std::quoted(Saved_Name) >> M.Columns >> M.Rows >> std::quoted(M.Orientation)) ||
               Magic != "APPEAL_SECTION" || Version != 1 || Saved_Name != M.Name ||
               !M.Columns || !M.Rows || M.Columns > 1000000 / M.Rows ||
               (M.Orientation != "Side_Scrolling" && M.Orientation != "Top_Down" && M.Orientation != "3D"))
                throw std::runtime_error("Invalid level tile-map dimensions/orientation");
            for(float &Value : M.Origin) if(!(Section >> Value) || !std::isfinite(Value)) throw std::runtime_error("Invalid map origin");
            for(float &Value : M.Size) if(!(Section >> Value) || !std::isfinite(Value) || Value <= 0) throw std::runtime_error("Invalid tile size");
            for(auto &Value : M.Neighbors) if(!(Section >> std::quoted(Value))) throw std::runtime_error("Missing neighbor");
            std::ostringstream Cells;
            for(std::size_t C = 0; C < M.Columns * M.Rows; ++C) {
                int Present = -1;
                if(!(Section >> Present) || (Present != 0 && Present != 1)) throw std::runtime_error("Invalid tile cell");
                if(Present) {
                    std::string Type, Entity_ID;
                    if(!(Section >> std::quoted(Type) >> Entity_ID)) throw std::runtime_error("Incomplete tile cell");
                    auto Key = std::make_pair(Type, ID(Entity_ID)); auto Found = Entities.find(Key);
                    if(Found == Entities.end()) throw std::runtime_error("Unresolved tile entity");
                    if(!Found->second && !Mutable_Tiles.insert(Key).second) throw std::runtime_error("Mutable tile entity repeated");
                    Cells << "1 " << std::quoted(Type) << ' ' << Key.second << '\n';
                } else Cells << "0\n";
            }
            Section >> std::ws;
            if(!Section.eof()) throw std::runtime_error("Extra tile-map cells");
            M.Cells = Cells.str(); Result.Maps.push_back(std::move(M));
        }
        In >> std::ws;
        if(!In.eof() || (!Result.Active.empty() && !Map_Names.count(Result.Active)))
            throw std::runtime_error("Extra level data or unknown active section");
        return Result;
    }
    void Create(const std::string &Name) {
        const std::string Maps = Identifier(Table(Name, "_Tile_Maps")), Entities = Identifier(Table(Name, "_Entities"));
        SQL("CREATE TABLE IF NOT EXISTS Levels (Name TEXT PRIMARY KEY, Tile_Map_Table TEXT NOT NULL UNIQUE, Entity_Table TEXT NOT NULL UNIQUE, World_Name TEXT NOT NULL DEFAULT '', Active_Section TEXT NOT NULL DEFAULT '', Paused INTEGER NOT NULL DEFAULT 0, Time_Scale REAL NOT NULL DEFAULT 1, Entity_Lists TEXT NOT NULL DEFAULT '', Format_Version INTEGER NOT NULL DEFAULT 0, Updated_At TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);");
        SQL("CREATE TABLE IF NOT EXISTS " + Maps + " (Name TEXT PRIMARY KEY, Columns INTEGER NOT NULL, Rows INTEGER NOT NULL, Orientation TEXT NOT NULL, Origin_X REAL NOT NULL, Origin_Y REAL NOT NULL, Origin_Z REAL NOT NULL, Tile_Size_X REAL NOT NULL, Tile_Size_Y REAL NOT NULL, Tile_Size_Z REAL NOT NULL, Neighbor_Left TEXT NOT NULL, Neighbor_Right TEXT NOT NULL, Neighbor_Above TEXT NOT NULL, Neighbor_Below TEXT NOT NULL, Neighbor_Front TEXT NOT NULL, Neighbor_Back TEXT NOT NULL, Cells TEXT NOT NULL);");
        SQL("CREATE TABLE IF NOT EXISTS " + Entities + " (Entity_Type TEXT NOT NULL, Entity_ID TEXT NOT NULL, Name TEXT NOT NULL UNIQUE, Immutable INTEGER NOT NULL CHECK(Immutable IN (0,1)), Data TEXT NOT NULL, PRIMARY KEY(Entity_Type, Entity_ID));");
        Query Insert("INSERT OR IGNORE INTO Levels(Name,Tile_Map_Table,Entity_Table) VALUES(?,?,?);");
        Insert.Bind(1, Name); Insert.Bind(2, Table(Name, "_Tile_Maps")); Insert.Bind(3, Table(Name, "_Entities")); Insert.Step();
    }
}

extern "C" void Create_Level(std::string Name)
{
    Level_Storage::Transaction Write;
    Level_Storage::Create(Name); Write.Commit();
}
extern "C" void Save_Level(std::string Name, std::string Snapshot)
{
    using namespace Level_Storage;
    Design Value = Parse(Snapshot);
    Transaction Write; Create(Name);
    SQL("DELETE FROM " + Identifier(Table(Name,"_Tile_Maps")) + "; DELETE FROM " + Identifier(Table(Name,"_Entities")) + ";");
    for(const auto &E : Value.Entities) {
        Query Insert("INSERT INTO " + Identifier(Table(Name,"_Entities")) + " VALUES(?,?,?,?,?);");
        Insert.Bind(1,E.Type); Insert.Bind(2,E.ID); Insert.Bind(3,E.Name); Insert.Bind(4,Number(E.Immutable)); Insert.Bind(5,E.Data); Insert.Step();
    }
    for(const auto &M : Value.Maps) {
        Query Insert("INSERT INTO " + Identifier(Table(Name,"_Tile_Maps")) + " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);");
        int I = 1; Insert.Bind(I++,M.Name); Insert.Bind(I++,Number(M.Columns)); Insert.Bind(I++,Number(M.Rows)); Insert.Bind(I++,M.Orientation);
        for(float V : M.Origin) Insert.Bind(I++,Number(V));
        for(float V : M.Size) Insert.Bind(I++,Number(V));
        for(const auto &V : M.Neighbors) Insert.Bind(I++,V);
        Insert.Bind(I,M.Cells); Insert.Step();
    }
    Query Update("UPDATE Levels SET World_Name=?,Active_Section=?,Paused=?,Time_Scale=?,Entity_Lists=?,Format_Version=1,Updated_At=CURRENT_TIMESTAMP WHERE Name=?;");
    Update.Bind(1,Value.World_Name); Update.Bind(2,Value.Active); Update.Bind(3,Number(Value.Paused)); Update.Bind(4,Number(Value.Time_Scale)); Update.Bind(5,Value.Type_Data); Update.Bind(6,Name); Update.Step();
    Write.Commit();
}
extern "C" std::string Load_Level(std::string Name)
{
    using namespace Level_Storage;
    Query Header("SELECT Tile_Map_Table,Entity_Table,World_Name,Active_Section,Paused,Time_Scale,Entity_Lists,Format_Version FROM Levels WHERE Name=?;");
    Header.Bind(1,Name);
    if(Header.Step() != SQLITE_ROW) throw std::runtime_error("Level not found: " + Name);
    if(Header.Text(0) != Table(Name,"_Tile_Maps") || Header.Text(1) != Table(Name,"_Entities") || Header.Text(7) != "1")
        throw std::runtime_error("Level has not been saved or its table registry is invalid");
    std::ostringstream Out;
    Out << "APPEAL_LEVEL 1\n" << std::quoted(Header.Text(2)) << ' ' << Header.Text(4) << ' ' << Header.Text(5) << ' ' << std::quoted(Header.Text(3)) << '\n' << Header.Text(6);
    std::vector<Entity_Record> Entities;
    Query Entity_Rows("SELECT Entity_Type,Entity_ID,Name,Immutable,Data FROM " + Identifier(Header.Text(1)) + " ORDER BY Entity_Type,Entity_ID;");
    while(Entity_Rows.Step() == SQLITE_ROW) {
        if(Entities.size() == 1000000) throw std::runtime_error("Too many level entities");
        Entity_Record E; E.Type=Entity_Rows.Text(0); E.ID=Entity_Rows.Text(1); E.Name=Entity_Rows.Text(2); E.Immutable=sqlite3_column_int(Entity_Rows.Stmt,3); E.Data=Entity_Rows.Text(4); Entities.push_back(std::move(E));
    }
    Out << Entities.size() << '\n';
    for(const auto &E : Entities) Out << std::quoted(E.Type) << ' ' << E.ID << ' ' << std::quoted(E.Name) << ' ' << E.Immutable << ' ' << std::quoted(E.Data) << '\n';
    std::vector<std::pair<std::string,std::string>> Maps;
    Query Map_Rows("SELECT Name,Columns,Rows,Orientation,Origin_X,Origin_Y,Origin_Z,Tile_Size_X,Tile_Size_Y,Tile_Size_Z,Neighbor_Left,Neighbor_Right,Neighbor_Above,Neighbor_Below,Neighbor_Front,Neighbor_Back,Cells FROM " + Identifier(Header.Text(0)) + " ORDER BY Name;");
    while(Map_Rows.Step() == SQLITE_ROW) {
        if(Maps.size() == 1000000) throw std::runtime_error("Too many level tile maps");
        std::ostringstream Section; const std::string Map_Name=Map_Rows.Text(0);
        Section << "APPEAL_SECTION 1\n" << std::quoted(Map_Name) << ' ' << Map_Rows.Text(1) << ' ' << Map_Rows.Text(2) << '\n' << std::quoted(Map_Rows.Text(3)) << '\n';
        for(int I=4;I<10;++I) Section << Map_Rows.Text(I) << ' ';
        Section << '\n';
        for(int I=10;I<16;++I) Section << std::quoted(Map_Rows.Text(I)) << '\n';
        Section << Map_Rows.Text(16); Maps.emplace_back(Map_Name,Section.str());
    }
    Out << Maps.size() << '\n';
    for(const auto &M : Maps) Out << std::quoted(M.first) << ' ' << std::quoted(M.second) << '\n';
    std::string Snapshot=Out.str(); Parse(Snapshot); // Validate before it reaches the live World.
    return Snapshot;
}
