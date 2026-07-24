import { createAdminClient } from "npm:@insforge/sdk";

type DeleteRequest = {
  id?: string;
};

export default async function(req: Request): Promise<Response> {
  if (req.method === "OPTIONS") {
    return new Response(null, { status: 204, headers: corsHeaders() });
  }

  if (req.method !== "POST") {
    return jsonResponse({ ok: false, error: "Method not allowed" }, 405);
  }

  // Reuse the toggle token as the shared write secret. The native client sends
  // X-Dashboard-Toggle-Token on delete requests too; both write endpoints are
  // gated by the same credential.
  const configuredToken = requiredEnv("DASHBOARD_TOGGLE_TOKEN");
  const receivedToken = req.headers.get("x-dashboard-toggle-token");
  if (!receivedToken || receivedToken !== configuredToken) {
    return jsonResponse({ ok: false, error: "Unauthorized" }, 401);
  }

  try {
    const body = await req.json() as DeleteRequest;
    const id = typeof body.id === "string" ? body.id.trim() : "";
    if (!id) return jsonResponse({ ok: false, error: "Missing item id" }, 400);

    const admin = createAdminClient({
      baseUrl: requiredEnv("INSFORGE_BASE_URL"),
      apiKey: requiredEnv("INSFORGE_API_KEY")
    });

    const { error } = await admin.database
      .from("planner_items")
      .delete()
      .eq("id", id);

    if (error) throw error;
    return jsonResponse({ ok: true, item: { id } });
  } catch (error) {
    return jsonResponse({ ok: false, error: errorMessage(error) }, 500);
  }
}

function corsHeaders(): HeadersInit {
  return {
    "Access-Control-Allow-Origin": "*",
    "Access-Control-Allow-Methods": "POST, OPTIONS",
    "Access-Control-Allow-Headers": "Content-Type, X-Dashboard-Toggle-Token"
  };
}

function requiredEnv(key: string): string {
  const value = Deno.env.get(key);
  if (!value) throw new Error(`Missing ${key}`);
  return value;
}

function jsonResponse(body: unknown, status = 200): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: {
      ...corsHeaders(),
      "Content-Type": "application/json; charset=utf-8",
      "Cache-Control": "no-store"
    }
  });
}

function errorMessage(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}
