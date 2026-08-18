# Platform targets

Nift v4.0.2 introduces explicit project-initialisation targets:

```bash
nift init
nift init --ext=.php
nift init --target=vercel
nift init --target=cloudflare
```

`nift init` defaults to `.html`. `--ext=.ext` sets both the default content and
output extension for the new project. The old positional spelling (`nift init
.html`) and `init-html` command are removed.

Platform targets intentionally initialise **static HTML sites**. When
`--target` is present, the accepted page extensions are `.html` and `.htm`.
This is a fail-fast contract: a command such as
`nift init --target=vercel --ext=.php` is rejected rather than creating a PHP
file that the selected static host would only serve as an inert asset.

## Target matrix

| Target | Nift output | Files created by `init` | What the platform can add around the static site |
|---|---|---|---|
| `vercel` | `.vercel/output/static/` | `.vercel/output/config.json` (Build Output API v3), `.gitignore` rule for generated static output | Vercel Functions, routing/project config, caching, environment variables, cron and other Vercel primitives |
| `amplify` | `.amplify-hosting/static/` | `.amplify-hosting/deploy-manifest.json` (deployment spec v1), `.gitignore` rule for generated static output | Amplify compute, routing, image optimisation, headers and build environment configuration |
| `netlify` | `public/` | `netlify.toml` with `nift build` and `public` publish directory | Functions, Edge Functions, redirects/rewrites, headers and other Netlify services |
| `azure` | `public/` | tracked `staticwebapp.config.json` source so Nift reproduces it in the deployment output | Azure Static Web Apps routing, auth/roles, headers, networking and optional Azure Functions APIs |
| `firebase` | `public/` | `firebase.json` with Hosting `public` directory and standard ignores | Redirects/rewrites, headers, Cloud Functions and Cloud Run integrations |
| `render` | `public/` | `render.yaml` static-site Blueprint | Redirects/rewrites, headers and separate Render services/databases |
| `cloudflare` | `public/` | `wrangler.toml` with `pages_build_output_dir = "./public"` | Pages Functions, Workers runtime features and bindings such as KV, D1, R2 and Durable Objects |
| `github-pages` | `public/` | no extra provider file; the target is deliberately an ordinary Pages-ready static artifact | GitHub Actions deployment, custom domains; dynamic APIs must live elsewhere |
| `supabase` | `public/` | no extra provider file; Nift keeps its ordinary static output | Supabase Postgres, Auth, Storage, Realtime and Edge Functions; deploy the frontend with a static host |

The CLI deliberately hides whether a provider uses a native deployment bundle,
a project config file, or simply Nift's normal `public/` tree. Users follow one
rule: if they know the destination, they can initialise with
`--target=<platform>`.

## Scope boundary

A target means:

> Initialise the smallest Nift project that is prepared for this platform's
> static deployment or backend-integration model.

It does **not** authenticate to the provider, create projects, configure DNS,
provision databases, or install Nift into the provider's remote build image.
Where a provider runs `nift build` itself, Nift must be made available in that
build environment by the user/project's chosen installation method. This is
kept separate from target generation so an initialised project is not silently
coupled to one Nift package channel or installer.

Provider-specific dynamic capabilities belong to the platform documentation,
not to Nift's parser or template language.

## File ownership

The Vercel and Amplify metadata files are source-controlled deployment metadata;
their static subdirectories are generated output and are ignored by Git.

Other generated platform files (`netlify.toml`, `firebase.json`, `render.yaml`,
`wrangler.toml`) are user-owned configuration after initialisation. Nift does
not rewrite them on ordinary builds.

Azure is the exception: `staticwebapp.config.json` must be present at the root
of the deployed output when a build step is used. Nift therefore creates
`content/staticwebapp.config.json` as a tracked raw JSON file and reproduces it
as `public/staticwebapp.config.json` on every build.

## Primary platform references

- Vercel Build Output API: https://vercel.com/docs/build-output-api
- Vercel Build Output configuration: https://vercel.com/docs/build-output-api/configuration
- Netlify file-based build configuration: https://docs.netlify.com/build/configure-builds/file-based-configuration/
- Netlify Frameworks API: https://docs.netlify.com/build/frameworks/frameworks-api/
- AWS Amplify Hosting deployment specification: https://docs.aws.amazon.com/amplify/latest/userguide/ssr-deployment-specification.html
- Azure Static Web Apps configuration: https://learn.microsoft.com/azure/static-web-apps/configuration
- Firebase Hosting configuration: https://firebase.google.com/docs/hosting/full-config
- Render Blueprint specification: https://render.com/docs/blueprint-spec
- Cloudflare Pages Wrangler configuration: https://developers.cloudflare.com/pages/functions/wrangler-configuration/
- GitHub Pages custom workflows: https://docs.github.com/pages/getting-started-with-github-pages/using-custom-workflows-with-github-pages
- Supabase deployment workflow: https://supabase.com/docs/guides/deployment
- Supabase Edge Functions quickstart: https://supabase.com/docs/guides/functions/quickstart
