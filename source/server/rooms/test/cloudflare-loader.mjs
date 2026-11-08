// Node-only builtin loader: production imports the real Cloudflare base class.
export function resolve(specifier, context, nextResolve) {
  if (specifier === "cloudflare:workers") return {
    url: "data:text/javascript,export class DurableObject { constructor(ctx, env) { this.ctx=ctx; this.env=env; } }",
    shortCircuit: true,
  };
  return nextResolve(specifier, context);
}
