import { fireEvent, render, screen, waitFor } from "@testing-library/react";
import {
  createConnectedMockZMKApp,
  ZMKAppProvider,
} from "@cormoran/zmk-studio-react-hook/testing";
import { call_rpc } from "@zmkfirmware/zmk-studio-ts-client";
import { RPCTestSection, SUBSYSTEM_IDENTIFIER } from "../../web/src/App";
import {
  ComboSource,
  Request,
  Response,
} from "../../web/src/proto/cormoran/runtime_combo/runtime_combo";

jest.mock("@zmkfirmware/zmk-studio-ts-client", () => ({ call_rpc: jest.fn() }));

const catalog = {
  combos: [
    {
      index: 0,
      name: "Synthetic combo",
      keyPositions: [0, 1],
      enabled: true,
      behavior: { behaviorId: 1 },
      source: ComboSource.COMBO_SOURCE_DEFAULT,
    },
  ],
};
const settings = {
  timeoutMs: 75,
  slowRelease: false,
  requirePriorIdleMs: 10,
  maxCombo: 8,
};
const encode = (response: Parameters<typeof Response.create>[0]) =>
  Response.encode(Response.create(response)).finish();
const reply = (payload: Uint8Array | undefined) => ({
  custom: { call: { payload } },
});

function install(
  respond: (
    request: Request,
    reads: { combos: number; settings: number },
  ) => Promise<ReturnType<typeof reply>> | ReturnType<typeof reply> | undefined,
) {
  const requests: Request[] = [];
  const reads = { combos: 0, settings: 0 };
  (call_rpc as jest.Mock).mockImplementation((_connection, request) => {
    if (!request.custom) return Promise.resolve({});
    const decoded = Request.decode(request.custom.call.payload);
    requests.push(decoded);
    if (decoded.listCombos) reads.combos++;
    if (decoded.getGlobalSettings) reads.settings++;
    const response = respond(decoded, reads);
    if (response !== undefined) return Promise.resolve(response);
    return Promise.resolve(
      reply(
        encode(
          decoded.listCombos
            ? { listCombos: catalog }
            : decoded.getGlobalSettings
              ? { getGlobalSettings: { settings } }
              : { status: {} },
        ),
      ),
    );
  });
  const app = createConnectedMockZMKApp({ subsystems: [SUBSYSTEM_IDENTIFIER] });
  render(
    <ZMKAppProvider value={app}>
      <RPCTestSection />
    </ZMKAppProvider>,
  );
  return requests;
}

async function loaded() {
  await screen.findByText("Synthetic combo");
  await waitFor(() =>
    expect(screen.getByRole("button", { name: "Save Combo" })).toBeEnabled(),
  );
}

beforeEach(() => jest.resetAllMocks());

const invalid = [
  ["missing payload", undefined, "Empty combo response"],
  ["zero-byte payload", new Uint8Array(), "Empty combo response"],
  [
    "unknown response",
    Uint8Array.from([0x78, 1]),
    "Unexpected Runtime Combo RPC response",
  ],
  [
    "another operation",
    encode({ listCombos: { combos: [] } }),
    "Unexpected Runtime Combo RPC response",
  ],
  [
    "mixed success responses",
    encode({ status: {}, listCombos: { combos: [] } }),
    "Unexpected Runtime Combo RPC response",
  ],
  [
    "mixed error and success",
    encode({ error: { message: "synthetic refusal" }, status: {} }),
    "Unexpected Runtime Combo RPC response",
  ],
  [
    "empty error",
    encode({ error: { message: "" } }),
    "Runtime Combo RPC request failed",
  ],
  [
    "whitespace error",
    encode({ error: { message: " \n " } }),
    "Runtime Combo RPC request failed",
  ],
] as const;

it.each(invalid)(
  "refuses %s before the follow-up combo-name mutation",
  async (_label, payload, error) => {
    const requests = install((request) =>
      request.setCombo ? reply(payload) : undefined,
    );
    await loaded();
    fireEvent.click(screen.getByText("Synthetic combo"));
    fireEvent.click(screen.getByRole("button", { name: "Save Combo" }));
    await waitFor(() =>
      expect(screen.getByRole("button", { name: "Save Combo" })).toBeEnabled(),
    );
    expect(requests.filter((request) => request.setCombo)).toHaveLength(1);
    expect(requests.filter((request) => request.setComboName)).toHaveLength(0);
    expect(requests.filter((request) => request.listCombos)).toHaveLength(1);
    await screen.findByText(error);
    expect(screen.getByLabelText("Name")).toHaveValue("Synthetic combo");
    expect(screen.getByLabelText("Max combos")).toHaveValue(8);
    expect(screen.queryByText("Combo saved")).not.toBeInTheDocument();
  },
);

it.each([
  "Save Timeout",
  "Save Slow Release",
  "Save Require Prior Idle",
  "Disable",
  "Reset to Default",
  "Save Pending",
  "Discard Pending",
])("requires a status acknowledgement for %s", async (action) => {
  const requests = install((request) =>
    !request.listCombos && !request.getGlobalSettings
      ? reply(encode({ listCombos: { combos: [] } }))
      : undefined,
  );
  await loaded();
  fireEvent.click(screen.getByText("Synthetic combo"));
  fireEvent.click(screen.getByRole("button", { name: action }));
  await waitFor(() =>
    expect(screen.getByRole("button", { name: action })).toBeEnabled(),
  );
  expect(requests).toHaveLength(3);
  await screen.findByText("Unexpected Runtime Combo RPC response");
  expect(screen.getByText("Synthetic combo")).toBeInTheDocument();
  expect(screen.getByLabelText("Max combos")).toHaveValue(8);
});

it("requires a status acknowledgement for the name write as well", async () => {
  const requests = install((request) =>
    request.setComboName ? reply(encode({ listCombos: {} })) : undefined,
  );
  await loaded();
  fireEvent.click(screen.getByRole("button", { name: "Save Combo" }));
  await screen.findByText("Unexpected Runtime Combo RPC response");
  expect(requests.filter((request) => request.setCombo)).toHaveLength(1);
  expect(requests.filter((request) => request.setComboName)).toHaveLength(1);
  expect(requests.filter((request) => request.listCombos)).toHaveLength(1);
  expect(screen.queryByText("Combo saved")).not.toBeInTheDocument();
});

it.each([
  ["missing settings", encode({ getGlobalSettings: {} })],
  ["another read", encode({ listCombos: {} })],
  ["mixed reads", encode({ getGlobalSettings: { settings }, listCombos: {} })],
])("preserves settings after %s", async (_label, payload) => {
  install((request, reads) =>
    request.getGlobalSettings && reads.settings > 1
      ? reply(payload)
      : undefined,
  );
  await loaded();
  fireEvent.click(screen.getAllByRole("button", { name: "Refresh" })[1]);
  await screen.findByText("Unexpected Runtime Combo RPC response");
  expect(screen.getByLabelText("Timeout ms")).toHaveValue(75);
  expect(screen.getByLabelText("Max combos")).toHaveValue(8);
});

it("preserves the catalog after a mixed list response", async () => {
  install((request, reads) =>
    request.listCombos && reads.combos > 1
      ? reply(encode({ listCombos: {}, status: {} }))
      : undefined,
  );
  await loaded();
  fireEvent.click(screen.getAllByRole("button", { name: "Refresh" })[0]);
  await waitFor(() =>
    expect(screen.getByRole("button", { name: "Save Combo" })).toBeEnabled(),
  );
  expect(screen.getByText("Synthetic combo")).toBeInTheDocument();
  await screen.findByText("Unexpected Runtime Combo RPC response");
});

it("contains a truncated protobuf response before any follow-up write", async () => {
  const requests = install((request) =>
    request.setCombo ? reply(Uint8Array.from([0x12, 0x80])) : undefined,
  );
  await loaded();
  fireEvent.click(screen.getByRole("button", { name: "Save Combo" }));
  await waitFor(() =>
    expect(screen.getByRole("button", { name: "Save Combo" })).toBeEnabled(),
  );
  expect(requests.filter((request) => request.setComboName)).toHaveLength(0);
  expect(
    document.querySelector(".editor > .message")?.textContent?.trim(),
  ).toBeTruthy();
});

it("shows the server's nonempty refusal and keeps the editor", async () => {
  install((request) =>
    request.setCombo
      ? reply(encode({ error: { message: "synthetic refusal" } }))
      : undefined,
  );
  await loaded();
  fireEvent.change(screen.getByLabelText("Name"), {
    target: { value: "Unsent name" },
  });
  fireEvent.click(screen.getByRole("button", { name: "Save Combo" }));
  await screen.findByText("synthetic refusal");
  expect(screen.getByLabelText("Name")).toHaveValue("Unsent name");
});

it("accepts an explicit empty catalog", async () => {
  install((request, reads) =>
    request.listCombos && reads.combos > 1
      ? reply(encode({ listCombos: {} }))
      : undefined,
  );
  await loaded();
  fireEvent.click(screen.getAllByRole("button", { name: "Refresh" })[0]);
  await screen.findByText("No runtime combos configured.");
  expect(screen.queryByText("Synthetic combo")).not.toBeInTheDocument();
});

it("accepts default-valued status messages for both combo writes", async () => {
  const requests = install(() => undefined);
  await loaded();
  fireEvent.click(screen.getByRole("button", { name: "Save Combo" }));
  await screen.findByText("Combo saved");
  expect(requests.filter((request) => request.setCombo)).toHaveLength(1);
  expect(requests.filter((request) => request.setComboName)).toHaveLength(1);
});

it.each(["Save Timeout", "Save Slow Release", "Save Require Prior Idle"])(
  "keeps independent global drafts and the persistence choice after %s",
  async (action) => {
    const stored = { ...settings };
    const requests = install((request) => {
      if (request.setTimeoutMs)
        stored.timeoutMs = request.setTimeoutMs.timeoutMs;
      if (request.setSlowRelease)
        stored.slowRelease = request.setSlowRelease.slowRelease;
      if (request.setRequirePriorIdleMs)
        stored.requirePriorIdleMs =
          request.setRequirePriorIdleMs.requirePriorIdleMs;
      if (request.getGlobalSettings)
        return reply(encode({ getGlobalSettings: { settings: stored } }));
      return undefined;
    });
    await loaded();
    fireEvent.change(screen.getByLabelText("Timeout ms"), {
      target: { value: "120" },
    });
    fireEvent.change(
      screen.getByLabelText("Require prior idle ms (0 disables)"),
      {
        target: { value: "222" },
      },
    );
    fireEvent.click(screen.getByRole("checkbox", { name: "Slow release" }));
    fireEvent.click(screen.getByLabelText("Persist global settings"));
    fireEvent.click(screen.getByRole("button", { name: action }));
    await waitFor(() =>
      expect(screen.getByRole("button", { name: action })).toBeEnabled(),
    );
    expect(screen.getByLabelText("Timeout ms")).toHaveValue(120);
    expect(
      screen.getByLabelText("Require prior idle ms (0 disables)"),
    ).toHaveValue(222);
    expect(
      screen.getByRole("checkbox", { name: "Slow release" }),
    ).toBeChecked();
    expect(screen.getByLabelText("Persist global settings")).toBeChecked();
    const mutation = requests.find(
      (request) =>
        request.setTimeoutMs ||
        request.setSlowRelease ||
        request.setRequirePriorIdleMs,
    );
    expect(
      mutation?.setTimeoutMs ??
        mutation?.setSlowRelease ??
        mutation?.setRequirePriorIdleMs,
    ).toEqual(
      action === "Save Timeout"
        ? { timeoutMs: 120, persist: true }
        : action === "Save Slow Release"
          ? { slowRelease: true, persist: true }
          : { requirePriorIdleMs: 222, persist: true },
    );
  },
);

it("keeps edits made while the initial settings read is pending", async () => {
  let finish!: (value: ReturnType<typeof reply>) => void;
  const pending = new Promise<ReturnType<typeof reply>>((resolve) => {
    finish = resolve;
  });
  install((request) => (request.getGlobalSettings ? pending : undefined));
  await screen.findByText("Synthetic combo");
  fireEvent.change(screen.getByLabelText("Timeout ms"), {
    target: { value: "321" },
  });
  fireEvent.click(screen.getByLabelText("Persist global settings"));
  finish(reply(encode({ getGlobalSettings: { settings } })));
  await waitFor(() =>
    expect(screen.getByLabelText("Max combos")).toHaveValue(8),
  );
  expect(screen.getByLabelText("Timeout ms")).toHaveValue(321);
  expect(screen.getByLabelText("Persist global settings")).toBeChecked();
});

it("keeps a newer edit while its previous value is being saved", async () => {
  let finish!: (value: ReturnType<typeof reply>) => void;
  const pending = new Promise<ReturnType<typeof reply>>((resolve) => {
    finish = resolve;
  });
  const requests = install((request) =>
    request.setTimeoutMs ? pending : undefined,
  );
  await loaded();
  fireEvent.change(screen.getByLabelText("Timeout ms"), {
    target: { value: "120" },
  });
  fireEvent.click(screen.getByRole("button", { name: "Save Timeout" }));
  await waitFor(() =>
    expect(requests.some((request) => request.setTimeoutMs)).toBe(true),
  );
  fireEvent.change(screen.getByLabelText("Timeout ms"), {
    target: { value: "321" },
  });
  finish(reply(encode({ status: {} })));
  await waitFor(() =>
    expect(screen.getByRole("button", { name: "Save Timeout" })).toBeEnabled(),
  );
  expect(
    requests.find((request) => request.setTimeoutMs)?.setTimeoutMs?.timeoutMs,
  ).toBe(120);
  expect(screen.getByLabelText("Timeout ms")).toHaveValue(321);
  expect(screen.getByText("Timeout saved")).toBeInTheDocument();
});

it.each(["Save Pending", "Discard Pending"])(
  "keeps edits made after %s starts and accepts its zero-count acknowledgement",
  async (action) => {
    let finish!: (value: ReturnType<typeof reply>) => void;
    const pending = new Promise<ReturnType<typeof reply>>((resolve) => {
      finish = resolve;
    });
    const requests = install((request) =>
      request.save || request.discard ? pending : undefined,
    );
    await loaded();
    fireEvent.click(screen.getByRole("button", { name: action }));
    await waitFor(() =>
      expect(requests.some((request) => request.save || request.discard)).toBe(
        true,
      ),
    );
    fireEvent.change(screen.getByLabelText("Timeout ms"), {
      target: { value: "321" },
    });
    finish(reply(encode({ status: {} })));
    await waitFor(() =>
      expect(screen.getByRole("button", { name: action })).toBeEnabled(),
    );
    expect(screen.getByLabelText("Timeout ms")).toHaveValue(321);
    expect(
      screen.getByText(
        action === "Save Pending"
          ? "Saved 0 runtime combo settings"
          : "Discarded 0 runtime combo settings",
      ),
    ).toBeInTheDocument();
  },
);
