const express = require('express');
const { pool } = require('../db');
const { requireAuth } = require('../middleware/auth');

const router = express.Router();

router.use(requireAuth);

// Kept in sync by hand with the client's own recognized set (mainwindow.cpp: iconForFileName()'s
// cppSourceExtensions/cppPlusPlusHeaderExtensions, plus the Save/Open dialog filters) -- C++
// source, headers (plain .h and the C++-flavored variants alike), and the docs commonly kept
// alongside C++ code. Anything else is rejected below before it reaches the database.
const ALLOWED_EXTENSIONS = ['.cpp', '.cc', '.cxx', '.c++', '.h', '.hpp', '.hh', '.hxx', '.h++', '.md', '.txt'];

// POST /files  { filename, content }
// Create-or-update semantics, keyed on (user_id, filename) -- mirrors the one-document-per-
// filename model the client already uses against Firestore in storage.cpp.
//
// A single atomic upsert, relying on the files_user_id_filename_key unique constraint
// (migrations/002_files_unique_per_user_filename.sql) -- not a separate UPDATE followed by a
// conditional INSERT, which left a race window where two near-simultaneous uploads of the same
// filename could both miss the UPDATE and both INSERT, producing two rows for one file.
router.post('/', async (req, res, next) => {
  const { filename, content } = req.body || {};
  if (!filename) {
    return res.status(400).json({ error: 'filename is required' });
  }
  if (!ALLOWED_EXTENSIONS.some((ext) => filename.toLowerCase().endsWith(ext))) {
    return res.status(400).json({ error: `Unsupported file type. Allowed: ${ALLOWED_EXTENSIONS.join(', ')}` });
  }

  try {
    const { rows } = await pool.query(
      `INSERT INTO files (user_id, filename, content)
       VALUES ($1, $2, $3)
       ON CONFLICT (user_id, filename)
       DO UPDATE SET content = EXCLUDED.content, updated_at = now()
       RETURNING id, filename, content, updated_at, (xmax = 0) AS inserted`,
      [req.userId, filename, content ?? '']
    );

    const { inserted, ...file } = rows[0];
    res.status(inserted ? 201 : 200).json(file);
  } catch (err) {
    next(err);
  }
});

// GET /files -- list the caller's files (metadata only, no content, to keep the listing light).
router.get('/', async (req, res, next) => {
  try {
    const { rows } = await pool.query(
      'SELECT id, filename, updated_at FROM files WHERE user_id = $1 ORDER BY updated_at DESC',
      [req.userId]
    );
    res.json(rows);
  } catch (err) {
    next(err);
  }
});

// GET /files/:id -- download one file's content, verifying ownership.
router.get('/:id', async (req, res, next) => {
  try {
    const { rows } = await pool.query(
      'SELECT id, filename, content, updated_at FROM files WHERE id = $1 AND user_id = $2',
      [req.params.id, req.userId]
    );
    if (rows.length === 0) {
      return res.status(404).json({ error: 'Not found' });
    }
    res.json(rows[0]);
  } catch (err) {
    next(err);
  }
});

// DELETE /files/:id -- verifies ownership before deleting.
router.delete('/:id', async (req, res, next) => {
  try {
    const { rowCount } = await pool.query(
      'DELETE FROM files WHERE id = $1 AND user_id = $2',
      [req.params.id, req.userId]
    );
    if (rowCount === 0) {
      return res.status(404).json({ error: 'Not found' });
    }
    res.status(204).end();
  } catch (err) {
    next(err);
  }
});

module.exports = router;
