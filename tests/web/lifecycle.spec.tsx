import {
  act,
  fireEvent,
  render,
  screen,
  waitFor,
} from "@testing-library/react";
import {
  createConnectedMockZMKApp,
  ZMKAppProvider,
} from "@cormoran/zmk-studio-react-hook/testing";
import { call_rpc } from "@zmkfirmware/zmk-studio-ts-client";
import { RPCTestSection, SUBSYSTEM_IDENTIFIER } from "../../web/src/App";
import {
  Request,
  Response,
} from "../../web/src/proto/cormoran/runtime_combo/runtime_combo";
jest.mock("@zmkfirmware/zmk-studio-ts-client", () => ({ call_rpc: jest.fn() }));
function reply(name: string) {
  return {
    custom: {
      call: {
        payload: Response.encode(
          Response.create({
            listCombos: {
              combos: [
                {
                  index: 0,
                  name,
                  keyPositions: [0, 1],
                  enabled: true,
                  behavior: { behaviorId: 1 },
                },
              ],
            },
          }),
        ).finish(),
      },
    },
  };
}
function deferred() {
  let resolve!: (v: ReturnType<typeof reply>) => void;
  let reject!: (e: Error) => void;
  const promise = new Promise<ReturnType<typeof reply>>((r, f) => {
    resolve = r;
    reject = f;
  });
  return { promise, resolve, reject };
}
function install(
  first: ReturnType<typeof createConnectedMockZMKApp>,
  pending: ReturnType<typeof deferred>,
  save = false,
) {
  let reads = 0;
  (call_rpc as jest.Mock).mockImplementation((connection, request) => {
    if (request.keymap)
      return Promise.resolve({
        keymap: { getPhysicalLayouts: { activeLayoutIndex: 0, layouts: [] } },
      });
    if (request.behaviors)
      return Promise.resolve({
        behaviors: { listAllBehaviors: { behaviors: [] } },
      });
    const decoded = Request.decode(request.custom.call.payload);
    if (decoded.getGlobalSettings)
      return Promise.resolve({
        custom: {
          call: {
            payload: Response.encode(
              Response.create({
                getGlobalSettings: { settings: { maxCombo: 8, timeoutMs: 50 } },
              }),
            ).finish(),
          },
        },
      });
    if (decoded.setCombo) return pending.promise;
    if (
      decoded.listCombos &&
      connection === first.state.connection &&
      ++reads === (save ? 99 : 2)
    )
      return pending.promise;
    return Promise.resolve(
      reply(
        connection === first.state.connection ? "first combo" : "second combo",
      ),
    );
  });
}
beforeEach(() => jest.resetAllMocks());
it.each(["success", "error"])(
  "retires old combo refresh %s after replacing the connection",
  async (outcome) => {
    const first = createConnectedMockZMKApp({
      subsystems: [SUBSYSTEM_IDENTIFIER],
    });
    const second = createConnectedMockZMKApp({
      subsystems: [SUBSYSTEM_IDENTIFIER],
    });
    const old = deferred();
    install(first, old);
    const view = render(
      <ZMKAppProvider value={first}>
        <RPCTestSection />
      </ZMKAppProvider>,
    );
    await screen.findByText("first combo");
    fireEvent.click(screen.getAllByRole("button", { name: "Refresh" })[0]);
    await waitFor(() =>
      expect(screen.getByRole("button", { name: "Save Combo" })).toBeDisabled(),
    );
    view.rerender(
      <ZMKAppProvider value={second}>
        <RPCTestSection />
      </ZMKAppProvider>,
    );
    await screen.findByText("second combo");
    await act(async () => {
      if (outcome === "success") old.resolve(reply("retired combo"));
      else old.reject(new Error("retired error"));
    });
    expect(screen.getByText("second combo")).toBeInTheDocument();
    expect(screen.queryByText("retired combo")).not.toBeInTheDocument();
    expect(screen.queryByText("retired error")).not.toBeInTheDocument();
  },
);
it("does not send a combo-name mutation or refresh after its save connection retires", async () => {
  const first = createConnectedMockZMKApp({
    subsystems: [SUBSYSTEM_IDENTIFIER],
  });
  const second = createConnectedMockZMKApp({
    subsystems: [SUBSYSTEM_IDENTIFIER],
  });
  const old = deferred();
  install(first, old, true);
  const view = render(
    <ZMKAppProvider value={first}>
      <RPCTestSection />
    </ZMKAppProvider>,
  );
  fireEvent.click(await screen.findByText("first combo"));
  await waitFor(() =>
    expect(screen.getByRole("button", { name: "Save Combo" })).toBeEnabled(),
  );
  fireEvent.click(screen.getByRole("button", { name: "Save Combo" }));
  await waitFor(() =>
    expect(
      (call_rpc as jest.Mock).mock.calls.some(
        ([, r]) => r.custom && Request.decode(r.custom.call.payload).setCombo,
      ),
    ).toBe(true),
  );
  view.rerender(
    <ZMKAppProvider value={second}>
      <RPCTestSection />
    </ZMKAppProvider>,
  );
  await screen.findByText("second combo");
  const count = (call_rpc as jest.Mock).mock.calls.length;
  await act(async () => old.resolve(reply("saved")));
  expect(call_rpc).toHaveBeenCalledTimes(count);
});
it("waits for both startup reads before enabling mutations", async () => {
  const app = createConnectedMockZMKApp({ subsystems: [SUBSYSTEM_IDENTIFIER] });
  const pending = deferred();
  (call_rpc as jest.Mock).mockImplementation((_connection, request) => {
    if (!request.custom) return Promise.resolve({});
    const decoded = Request.decode(request.custom.call.payload);
    return decoded.getGlobalSettings
      ? pending.promise
      : Promise.resolve(reply("loaded combo"));
  });
  render(
    <ZMKAppProvider value={app}>
      <RPCTestSection />
    </ZMKAppProvider>,
  );
  await screen.findByText("loaded combo");
  expect(screen.getByRole("button", { name: "Save Combo" })).toBeDisabled();
  await act(async () => pending.resolve(reply("other")));
  expect(screen.getByRole("button", { name: "Save Combo" })).toBeEnabled();
});
