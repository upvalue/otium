// Pin normal service for OpenAI without changing other providers' payloads.
export default function docsPi(pi) {
  pi.on('before_provider_request', (event, ctx) => {
    if (['openai-responses', 'openai-codex-responses'].includes(ctx.model?.api)) {
      return { ...event.payload, service_tier: 'default' };
    }
  });
}
