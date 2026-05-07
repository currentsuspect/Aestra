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

export async function sendLoginChallenge(env: Env, email: string, challenge: CreatedLoginChallenge):
    Promise<LoginDelivery> {
  void email;
  void challenge;

  if (env.AESTRA_LOGIN_MAILER_MODE === "fixture") {
    return { exposeCode: true };
  }
  if (env.AESTRA_LOGIN_MAILER_MODE === "configured") {
    return { exposeCode: false };
  }

  throw new MailerError(500, "mailer_unconfigured", "login mailer is not configured");
}
