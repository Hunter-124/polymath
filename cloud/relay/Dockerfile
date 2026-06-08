# ── Polymath relay — multi-stage build ──────────────────────────────────────
# Stage 1 compiles the TypeScript; stage 2 is a slim runtime with only the
# production deps and the emitted JS. No source, no devDeps in the final image.

# ---- build ----
FROM node:20-slim AS build
WORKDIR /app

# Install ALL deps (incl. dev) against the lockfile for a reproducible build.
COPY package.json package-lock.json ./
RUN npm ci

# Compile.
COPY tsconfig.json ./
COPY src ./src
RUN npm run build

# Prune to production deps only, to copy into the runtime stage.
RUN npm prune --omit=dev

# ---- runtime ----
FROM node:20-slim AS runtime
ENV NODE_ENV=production
WORKDIR /app

# Drop root.
USER node

COPY --chown=node:node --from=build /app/node_modules ./node_modules
COPY --chown=node:node --from=build /app/dist ./dist
COPY --chown=node:node package.json ./

# The app reads PORT from the env (default 8080). Hosts that inject their own
# PORT (Fly/Render) override this; EXPOSE is documentation only.
ENV PORT=8080
EXPOSE 8080

CMD ["node", "dist/index.js"]
