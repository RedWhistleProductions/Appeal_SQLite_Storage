# Appeal_SQLite_Storage

An Appeal plugin project.

## Development status

This repository was separated from the Appeal beta development tree. Its build and installer configuration will be modernized before the first standalone release.

Generated binaries and intermediate objects are intentionally excluded from version control.


## Schema setup

Execute_SQL(SQL) executes SQL on the selected database without returning query rows.
On failure it throws and rolls back any active transaction. See
[database setup](../../../Appeal/Setup/Databases.md) for independently callable
User_Setings and Test_Game initialization scripts.


Get_Field(Table, Key_Column, Key, Field) returns text from an actual SQL table.
Set_Field(Table, Key_Column, Key, Field, Value) upserts one field. Identifiers
are validated; keys and values use bound parameters. Missing rows/NULL read
as empty text. See the controller GUI documentation for Control_Sets usage.
