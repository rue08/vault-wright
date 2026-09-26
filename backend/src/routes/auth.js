const express = require('express');
const { verifyFirebaseIdToken } = require('../firebaseAuth');
const { pool } = require('../db');
const { requireAuth } = require('../middleware/auth');

const router = express.Router();

/**
 * @openapi
 * /auth/firebase/login:
 *   post:
 *     tags: [Auth]
 *     summary: Register or refresh the account for a Firebase ID token
 *     description: >
 *       Verifies the Firebase ID token and upserts a `users` row keyed on the Firebase UID. Must be
 *       called once after Firebase sign-in or sign-up, before any other route will accept the token.
 *     security: []
 *     requestBody:
 *       required: true
 *       content:
 *         application/json:
 *           schema:
 *             type: object
 *             required: [id_token]
 *             properties:
 *               id_token: { type: string, description: Firebase ID token }
 *     responses:
 *       200:
 *         description: The account record
 *         content:
 *           application/json:
 *             schema:
 *               $ref: '#/components/schemas/User'
 *       400:
 *         description: "`id_token` missing"
 *         content:
 *           application/json:
 *             schema: { $ref: '#/components/schemas/Error' }
 *             example: { error: id_token is required }
 *       401:
 *         $ref: '#/components/responses/Unauthorized'
 *       500:
 *         $ref: '#/components/responses/InternalError'
 */
// POST /auth/firebase/login  { id_token }
// Verifies the Firebase ID token, upserts a users row keyed on firebase_uid, and returns the
// canonical user record. Must be called once after Firebase sign-in/sign-up before any other
// route will accept the token (requireAuth rejects tokens with no matching users row).
router.post('/firebase/login', async (req, res, next) => {
  const { id_token: idToken } = req.body || {};
  if (!idToken) {
    return res.status(400).json({ error: 'id_token is required' });
  }

  let decoded;
  try {
    decoded = await verifyFirebaseIdToken(idToken);
  } catch (err) {
    return res.status(401).json({ error: 'Invalid or expired token' });
  }

  try {
    const { rows } = await pool.query(
      `INSERT INTO users (email, firebase_uid)
       VALUES ($1, $2)
       ON CONFLICT (firebase_uid)
       DO UPDATE SET email = EXCLUDED.email
       RETURNING id, email, firebase_uid, created_at, is_active`,
      [decoded.email || null, decoded.sub]
    );

    res.json(rows[0]);
  } catch (err) {
    next(err);
  }
});

/**
 * @openapi
 * /auth/account:
 *   delete:
 *     tags: [Auth]
 *     summary: Permanently delete the account and all its cloud files
 *     description: >
 *       Deletes the caller's `users` row; every file they own is removed with it (`ON DELETE CASCADE`).
 *       Call this **before** deleting the Firebase identity, since it needs a still-valid token. The
 *       Firebase identity itself is deleted by the client directly against Firebase, not by this API.
 *     responses:
 *       204:
 *         description: Account and files deleted
 *       401:
 *         $ref: '#/components/responses/Unauthorized'
 *       403:
 *         $ref: '#/components/responses/Forbidden'
 *       500:
 *         $ref: '#/components/responses/InternalError'
 */
// DELETE /auth/account -- permanently deletes the caller's users row. `files.user_id` has
// ON DELETE CASCADE (migrations/001_init.sql), so this also wipes every cloud file they own in
// the same statement -- no separate file-cleanup pass needed. The client only calls this after
// confirming with the user; it's the first step of "Delete Account", followed by deleting the
// Firebase identity itself (see Authenticator::deleteAccount() -- outside this backend's reach).
router.delete('/account', requireAuth, async (req, res, next) => {
  try {
    await pool.query('DELETE FROM users WHERE id = $1', [req.userId]);
    res.status(204).end();
  } catch (err) {
    next(err);
  }
});

module.exports = router;
