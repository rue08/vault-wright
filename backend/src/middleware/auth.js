const { verifyFirebaseIdToken } = require('../firebaseAuth');
const { pool } = require('../db');

// Verifies the Bearer idToken, looks up the corresponding users row by firebase_uid, and
// attaches req.userId. Rejects if /auth/firebase/login was never called for this user (no
// matching row) or the account has been disabled (is_active = false).
async function requireAuth(req, res, next) {
  const header = req.headers.authorization || '';
  const [scheme, idToken] = header.split(' ');

  if (scheme !== 'Bearer' || !idToken) {
    return res.status(401).json({ error: 'Missing Bearer idToken' });
  }

  let decoded;
  try {
    decoded = await verifyFirebaseIdToken(idToken);
  } catch (err) {
    return res.status(401).json({ error: 'Invalid or expired token' });
  }

  try {
    const { rows } = await pool.query(
      'SELECT id, is_active FROM users WHERE firebase_uid = $1',
      [decoded.sub]
    );

    if (rows.length === 0) {
      return res.status(403).json({ error: 'No account on file for this user' });
    }
    if (!rows[0].is_active) {
      return res.status(403).json({ error: 'Account disabled' });
    }

    req.userId = rows[0].id;
    req.firebaseUid = decoded.sub;
    next();
  } catch (err) {
    next(err);
  }
}

module.exports = { requireAuth };
