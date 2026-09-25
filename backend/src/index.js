require('dotenv').config();

const express = require('express');
const { pool } = require('./db');
const { requireAuth } = require('./middleware/auth');
const authRoutes = require('./routes/auth');
const fileRoutes = require('./routes/files');

const app = express();
app.use(express.json({ limit: '25mb' })); // file content is stored/transferred as JSON text

app.get('/health', (req, res) => res.json({ ok: true }));

app.use('/auth', authRoutes);
app.use('/files', fileRoutes);

// DELETE /account -- deletes the users row (cascades to files via FK). Client calls this
// *before* separately calling Firebase's own accounts:delete REST endpoint directly -- order
// matters, this still needs a valid token to authenticate the deletion request.
app.delete('/account', requireAuth, async (req, res, next) => {
  try {
    await pool.query('DELETE FROM users WHERE id = $1', [req.userId]);
    res.status(204).end();
  } catch (err) {
    next(err);
  }
});

// Centralized error handler -- every route above forwards unexpected failures via next(err).
app.use((err, req, res, next) => {
  console.error(err);
  res.status(500).json({ error: 'Internal server error' });
});

const port = process.env.PORT || 3000;
app.listen(port, () => {
  console.log(`vw-backend listening on port ${port}`);
});
