const express = require('express');
const { apiReference } = require('@scalar/express-api-reference');
const openapiSpec = require('../config/openapi');

const router = express.Router();

// GET /openapi.json -- the raw generated spec, same document Scalar renders below.
router.get('/openapi.json', (req, res) => {
  res.json(openapiSpec);
});

// GET /docs -- Scalar's API reference UI, reading the spec from the route above rather than a
// static copy, so it can never drift from it.
router.get(
  '/docs',
  apiReference({
    url: '/openapi.json',
  })
);

module.exports = router;
