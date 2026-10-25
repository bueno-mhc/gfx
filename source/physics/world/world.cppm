/// @file world.cppm
/// @brief `import gfx.physics.world;`: the framework the library's physics backends are built on — the edits a world
///        records (:edits), the snapshots of its steps (:snapshot), and SteppedWorld with what a backend implements
///        for it (:stepped_world). A custom backend implements SteppedWorldBackend and gets deferred edits, generic
///        objects, stepping on its own thread, the reader handshake and the abstraction's queries for free.
export module gfx.physics.world;

export import gfx.physics.interface;
export import :edits;
export import :snapshot;
export import :stepped_world;
