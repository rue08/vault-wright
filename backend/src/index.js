require('dotenv').config();

const express = require('express');
const authRoutes = require('./routes/auth');
const fileRoutes = require('./routes/files');
const docsRoutes = require('./routes/docs');

const app = express();
app.use(express.json({ limit: '25mb' })); // file content is stored/transferred as JSON text

/**
 * @openapi
 * /health:
 *   get:
 *     tags: [Health]
 *     summary: Liveness probe
 *     security: []
 *     responses:
 *       200:
 *         description: The server is up
 *         content:
 *           application/json:
 *             schema:
 *               type: object
 *               properties:
 *                 ok: { type: boolean, example: true }
 */
app.get('/health', (req, res) => res.json({ ok: true }));

app.use(docsRoutes);
app.use('/auth', authRoutes);
app.use('/files', fileRoutes);

// Unmatched URL or method -- JSON 404 instead of Express's default HTML page.
app.use((req, res) => {
  res.status(404).json({ error: 'not found' });
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
