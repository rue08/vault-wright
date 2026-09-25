// Verifies Firebase ID tokens using Google's public JWKS -- no Firebase Admin SDK / service
// account secret required, keeping this server secret-free the same way the rest of the
// project's Firebase usage is (see config.h / authenticator.cpp using only the public API key).

const jwt = require('jsonwebtoken');
const jwksClient = require('jwks-rsa');

// The Firebase project whose tokens this server accepts. No default on purpose: a server
// silently trusting somebody else's project is worse than one that refuses to start.
const FIREBASE_PROJECT_ID = process.env.FIREBASE_PROJECT_ID;
if (!FIREBASE_PROJECT_ID) {
  throw new Error('FIREBASE_PROJECT_ID is not set -- add it to .env (Firebase Console -> Project settings -> General -> Project ID)');
}
const ISSUER = `https://securetoken.google.com/${FIREBASE_PROJECT_ID}`;
const JWKS_URI = 'https://www.googleapis.com/service_accounts/v1/jwk/securetoken@system.gserviceaccount.com';

const client = jwksClient({
  jwksUri: JWKS_URI,
  cache: true,
  cacheMaxAge: 6 * 60 * 60 * 1000, // 6h, well under Firebase's key rotation cadence
  rateLimit: true,
});

function getSigningKey(header, callback) {
  client.getSigningKey(header.kid, (err, key) => {
    if (err) return callback(err);
    callback(null, key.getPublicKey());
  });
}

/**
 * Verifies a Firebase ID token's signature, issuer, audience, and expiry.
 * Resolves with the decoded claims (including `uid`, `email`) or rejects on any failure.
 */
function verifyFirebaseIdToken(idToken) {
  return new Promise((resolve, reject) => {
    jwt.verify(
      idToken,
      getSigningKey,
      {
        algorithms: ['RS256'],
        issuer: ISSUER,
        audience: FIREBASE_PROJECT_ID,
      },
      (err, decoded) => {
        if (err) return reject(err);
        if (!decoded.sub) return reject(new Error('Token missing sub claim'));
        resolve(decoded);
      }
    );
  });
}

module.exports = { verifyFirebaseIdToken, FIREBASE_PROJECT_ID };
