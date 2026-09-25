-- Closes the race window in the old POST /files handler (UPDATE, then INSERT only if the
-- UPDATE matched nothing): two near-simultaneous uploads of the same filename could both miss
-- the UPDATE and both INSERT, leaving two rows for what should be one file. A unique constraint
-- makes that impossible at the database level, and lets the route use a single atomic
-- `INSERT ... ON CONFLICT DO UPDATE` instead of two separate statements.

-- Defensive dedup first: ADD CONSTRAINT below fails outright if any duplicate (user_id,
-- filename) pairs already exist. Keep the most recently updated row for each pair and drop the
-- rest -- this is a no-op if there are no duplicates.
DELETE FROM files f
    USING files newer
    WHERE f.user_id = newer.user_id
      AND f.filename = newer.filename
      AND f.id <> newer.id
      AND (f.updated_at, f.id) < (newer.updated_at, newer.id);

ALTER TABLE files
    ADD CONSTRAINT files_user_id_filename_key UNIQUE (user_id, filename);
