import type { CreatedLoginChallenge } from "./loginChallengeStore";
import type { Env } from "./signing";

export class MailerError extends Error {
  readonly status: number;
  readonly code: string;

  constructor(status: number, code: string, message: string) {
    super(message);
    this.name = "MailerError";
    this.status = status;
    this.code = code;
  }
}

export type LoginDelivery = {
  exposeCode: boolean;
};

export function assertLoginMailerConfigured(env: Env): void {
  if (env.AESTRA_LOGIN_MAILER_MODE === "fixture") {
    return;
  }
  if ((env.AESTRA_LOGIN_MAILER_MODE as string | undefined) === "configured" || env.AESTRA_LOGIN_MAILER_MODE === "smtp") {
    if (!env.AESTRA_RESEND_API_KEY) {
      throw new MailerError(500, "resend_key_missing", "AESTRA_RESEND_API_KEY is not set");
    }
    return;
  }

  throw new MailerError(500, "mailer_unconfigured", "login mailer is not configured");
}

async function resendSend(env: Env, toEmail: string, code: string, expiresAt: number): Promise<void> {
  assertLoginMailerConfigured(env);

  const expiresMinutes = Math.ceil((expiresAt * 1000 - Date.now()) / 60000);
  const html = buildLoginEmail(code, expiresMinutes);

  const response = await fetch("https://api.resend.com/emails", {
    method: "POST",
    headers: {
      "Authorization": `Bearer ${env.AESTRA_RESEND_API_KEY}`,
      "Content-Type": "application/json",
    },
    body: JSON.stringify({
      from: "Aestra Studio <no-reply@aestra.studio>",
      to: [toEmail.trim()],
      subject: "Your Aestra Login Code",
      html,
    }),
  });

  if (!response.ok) {
    const text = await response.text().catch(() => "");
    throw new MailerError(502, "mailer_error", `Resend API error ${response.status}: ${text}`);
  }
}

function buildLoginEmail(code: string, expiresMinutes: number): string {
  const accent = "#7c3aed";
  const accentHover = "#6d28d9";
  const bg = "#09090b";
  const surface = "#18181b";
  const surfaceBorder = "#27272a";
  const text = "#fafafa";
  const textMuted = "#a1a1aa";
  const textSubtle = "#71717a";
  const white = "#ffffff";
  const green = "#22c55e";

  const codeDigits = code.split("").join(" ");
  const expiresLabel = expiresMinutes >= 60
    ? "60 minutes"
    : `${expiresMinutes} minute${expiresMinutes !== 1 ? "s" : ""}`;

  return `<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<meta http-equiv="X-UA-Compatible" content="IE=edge">
<title>Your Aestra Login Code</title>
<!--[if mso]>
<style type="text/css">
  table { border-collapse: collapse; }
  .btn { padding: 12px 24px !important; }
</style>
<![endif]-->
</head>
<body style="margin:0;padding:0;background-color:${bg};font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,'Helvetica Neue',Arial,sans-serif;color:${text};-webkit-font-smoothing:antialiased;-moz-osx-font-smoothing:grayscale;">
  <!--[if mso]><table role="presentation" width="100%" cellpadding="0" cellspacing="0" border="0"><tr><td style="padding:48px 16px 32px;"><![endif]-->
  <!--[if !mso]><!-->
  <div style="width:100%;background-color:${bg};padding:48px 16px 32px;text-align:center;">
  <!--<![endif]-->
    <!-- Logo -->
    <div style="padding-bottom:28px;">
      <img src="https://www.aestra.studio/logo.png" alt="Aestra" width="56" height="56" style="border-radius:14px;display:inline-block;" class="logo">
    </div>

    <!-- Card -->
    <div style="max-width:480px;margin:0 auto;background-color:${surface};border:1px solid ${surfaceBorder};border-radius:20px;overflow:hidden;">

      <!-- Card header -->
      <div style="padding:32px 24px 24px;border-bottom:1px solid ${surfaceBorder};">
        <p style="margin:0 0 8px;font-size:12px;font-weight:600;letter-spacing:0.1em;text-transform:uppercase;color:${textSubtle};">Sign in to Aestra</p>
        <h1 style="margin:0;font-size:20px;font-weight:600;color:${white};line-height:1.3;">Enter this code to continue</h1>
      </div>

      <!-- Code block -->
      <div style="padding:28px 24px 0;background-color:${bg};">
        <p style="margin:0 0 18px;font-size:14px;color:${textMuted};text-align:center;line-height:1.5;">
          Your verification code is below.<br>It expires in ${expiresLabel}.
        </p>
        <div style="display:inline-block;background-color:${surface};border:1px solid ${surfaceBorder};border-radius:12px;padding:18px 24px;letter-spacing:0.35em;font-size:30px;font-weight:700;color:${white};font-variant-numeric:tabular-nums;line-height:1;">
          ${codeDigits}
        </div>
      </div>

      <!-- Expiry notice -->
      <div style="padding:16px 24px 28px;">
        <div style="display:inline-flex;align-items:center;gap:6px;background-color:rgba(34,197,94,0.1);border:1px solid rgba(34,197,94,0.2);border-radius:8px;padding:8px 12px;">
          <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="${green}" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
            <circle cx="12" cy="12" r="10"></circle>
            <polyline points="12 6 12 12 16 14"></polyline>
          </svg>
          <span style="font-size:12px;color:${green};font-weight:500;">Valid for ${expiresLabel}</span>
        </div>
      </div>

      <!-- Security note -->
      <div style="padding:0 24px 32px;">
        <div style="background-color:rgba(124,58,237,0.06);border:1px solid rgba(124,58,237,0.15);border-radius:10px;padding:14px 16px;text-align:left;">
          <p style="margin:0 0 4px;font-size:11px;font-weight:600;color:${textMuted};text-transform:uppercase;letter-spacing:0.06em;">Security notice</p>
          <p style="margin:0;font-size:13px;color:${textSubtle};line-height:1.5;">
            If you didn't request this code, you can safely ignore this email. Never share this code with anyone — Aestra will never ask for it.
          </p>
        </div>
      </div>

    </div>

    <!-- Footer -->
    <div style="padding-top:28px;max-width:480px;margin-left:auto;margin-right:auto;">
      <p style="margin:0 0 6px;font-size:13px;color:${textMuted};font-weight:600;letter-spacing:0.04em;">Aestra Studio</p>
      <p style="margin:0 0 12px;font-size:12px;color:${textSubtle};line-height:1.5;">
        Make music, not excuses.
      </p>
      <p style="margin:0;font-size:11px;color:${textSubtle};">
        &copy; 2026 Aestra Studios &bull; <a href="https://aestra.studio" style="color:${accent};text-decoration:none;">aestra.studio</a>
      </p>
    </div>
  <!--[if !mso]><!--></div><!--<![endif]-->
  <!--[if mso]></td></tr></table><![endif]-->
</body>
</html>`;
}

export async function sendLoginChallenge(env: Env, email: string, challenge: CreatedLoginChallenge):
    Promise<LoginDelivery> {
  assertLoginMailerConfigured(env);
  if (env.AESTRA_LOGIN_MAILER_MODE === "fixture") {
    return { exposeCode: true };
  }
  if ((env.AESTRA_LOGIN_MAILER_MODE as string | undefined) === "configured" || env.AESTRA_LOGIN_MAILER_MODE === "smtp") {
    await resendSend(env, email, challenge.code, challenge.expiresAt);
    return { exposeCode: false };
  }
  throw new MailerError(500, "mailer_unconfigured", "login mailer is not configured");
}
