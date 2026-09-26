const swaggerJsdoc = require('swagger-jsdoc');
const path = require('path');
const { version } = require('../../package.json');

// Builds the OpenAPI document from the `@openapi` JSDoc blocks living above each route handler
// in src/index.js and src/routes/*.js -- swagger-jsdoc only reads those files as text, it never
// requires/executes them, so generating this spec never touches Postgres or Firebase.
const spec = swaggerJsdoc({
  definition: {
    openapi: '3.1.0',
    info: {
      title: 'VaultWright API',
      version,
      description: [
        'Backend for VaultWright: stores each signed-in user\'s cloud files in Postgres. Identity comes from Firebase Authentication; this server only verifies Firebase ID tokens.',
        '',
        '**Authentication.** Every route except `/health` and `POST /auth/firebase/login` expects `Authorization: Bearer <Firebase ID token>`. A valid token is not enough on its own: it must belong to a user who has already called `POST /auth/firebase/login`, otherwise the request is rejected with `403`.',
        '',
        '**Typical flow.**',
        '',
        '1. Sign in with Firebase and obtain an ID token.',
        '2. Call `POST /auth/firebase/login` once with that token. This creates (or updates) the account record.',
        '3. Send the ID token as a Bearer token on every `/files` call.',
        '',
        '**Deleting an account.** Call `DELETE /auth/account` *first*: it needs a still-valid token, and it also removes every file the user owns. Deleting the Firebase identity itself is done by the client directly against Firebase afterwards; this API never does it.',
        '',
        '**File types.** Only C++-related files are accepted: `.cpp`, `.cc`, `.cxx`, `.c++`, `.h`, `.hpp`, `.hh`, `.hxx`, `.h++`, `.md`, `.txt`. Request bodies are limited to 25 MB.',
      ].join('\n'),
    },
    // Applies to every operation unless a route overrides it with its own `security: []`.
    security: [{ BearerAuth: [] }],
    tags: [
      { name: 'Health', description: 'Liveness probe.' },
      { name: 'Auth', description: 'Register a Firebase-authenticated user and delete their account.' },
      { name: 'Files', description: 'The signed-in user\'s cloud files.' },
    ],
    components: {
      securitySchemes: {
        BearerAuth: {
          type: 'http',
          scheme: 'bearer',
          bearerFormat: 'Firebase ID token',
          description: 'Firebase ID token from the user\'s sign-in.',
        },
      },
      // Shared error responses, referenced from the route blocks so each status is described once.
      responses: {
        Unauthorized: {
          description: 'Missing, malformed, invalid or expired bearer token',
          content: {
            'application/json': {
              schema: { $ref: '#/components/schemas/Error' },
              examples: {
                missing: { summary: 'No Authorization header', value: { error: 'Missing Bearer idToken' } },
                invalid: { summary: 'Bad or expired token', value: { error: 'Invalid or expired token' } },
              },
            },
          },
        },
        Forbidden: {
          description: 'The token is valid but the account can\'t be used',
          content: {
            'application/json': {
              schema: { $ref: '#/components/schemas/Error' },
              examples: {
                noAccount: {
                  summary: 'POST /auth/firebase/login was never called for this user',
                  value: { error: 'No account on file for this user' },
                },
                disabled: { summary: 'Account disabled', value: { error: 'Account disabled' } },
              },
            },
          },
        },
        BadRequest: {
          description: 'Missing or invalid request field',
          content: {
            'application/json': {
              schema: { $ref: '#/components/schemas/Error' },
              examples: {
                missingFilename: { summary: 'No filename', value: { error: 'filename is required' } },
                badType: {
                  summary: 'Extension not allowed',
                  value: {
                    error: 'Unsupported file type. Allowed: .cpp, .cc, .cxx, .c++, .h, .hpp, .hh, .hxx, .h++, .md, .txt',
                  },
                },
              },
            },
          },
        },
        NotFound: {
          description: 'No such file, or it belongs to another account',
          content: {
            'application/json': {
              schema: { $ref: '#/components/schemas/Error' },
              example: { error: 'Not found' },
            },
          },
        },
        InternalError: {
          description: 'Unexpected server error',
          content: {
            'application/json': {
              schema: { $ref: '#/components/schemas/Error' },
              example: { error: 'Internal server error' },
            },
          },
        },
      },
      schemas: {
        User: {
          type: 'object',
          description: 'The account record for a Firebase user.',
          properties: {
            id: { type: 'integer', example: 42 },
            email: { type: 'string', nullable: true, example: 'someone@example.com' },
            firebase_uid: { type: 'string', example: 'kR3vN8xQmPZ2aTd9LwYb6HcJ0eU1' },
            created_at: { type: 'string', format: 'date-time' },
            is_active: { type: 'boolean', example: true },
          },
        },
        FileInput: {
          type: 'object',
          required: ['filename'],
          properties: {
            filename: { type: 'string', description: 'Must end in one of the allowed extensions', example: 'main.cpp' },
            content: { type: 'string', description: 'File text. Defaults to an empty string.', example: '#include <iostream>\nint main() {}\n' },
          },
        },
        File: {
          type: 'object',
          properties: {
            id: { type: 'integer', example: 7 },
            filename: { type: 'string', example: 'main.cpp' },
            content: { type: 'string', example: '#include <iostream>\nint main() {}\n' },
            updated_at: { type: 'string', format: 'date-time' },
          },
        },
        FileSummary: {
          type: 'object',
          description: 'A file without its content, as returned by the listing.',
          properties: {
            id: { type: 'integer', example: 7 },
            filename: { type: 'string', example: 'main.cpp' },
            updated_at: { type: 'string', format: 'date-time' },
          },
        },
        Error: {
          type: 'object',
          properties: {
            error: { type: 'string' },
          },
        },
      },
    },
  },
  apis: [path.join(__dirname, '../index.js'), path.join(__dirname, '../routes/*.js')],
});

module.exports = spec;
