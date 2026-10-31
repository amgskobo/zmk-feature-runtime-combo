import { act, renderHook, waitFor } from "@testing-library/react";
import { call_rpc } from "@zmkfirmware/zmk-studio-ts-client";
import type { RpcConnection } from "@zmkfirmware/zmk-studio-ts-client";
import {
  usePhysicalLayoutKeys,
  useBehaviorOptions,
} from "../../web/src/useStudioCore";
jest.mock("@zmkfirmware/zmk-studio-ts-client", () => ({ call_rpc: jest.fn() }));
const connection = () => ({}) as RpcConnection;
beforeEach(() => jest.resetAllMocks());
it("hides old physical positions while a replacement connection loads", async () => {
  const first = connection(),
    next = connection();
  let finish!: (v: unknown) => void;
  const pending = new Promise((r) => {
    finish = r;
  });
  (call_rpc as jest.Mock)
    .mockResolvedValueOnce({
      keymap: {
        getPhysicalLayouts: {
          activeLayoutIndex: 0,
          layouts: [{ keys: [{ x: 100, width: 100 }] }],
        },
      },
    })
    .mockReturnValueOnce(pending);
  const view = renderHook(({ source }) => usePhysicalLayoutKeys(source), {
    initialProps: { source: first },
  });
  await waitFor(() => expect(view.result.current).toHaveLength(1));
  view.rerender({ source: next });
  expect(view.result.current).toBeNull();
  await act(async () =>
    finish({
      keymap: { getPhysicalLayouts: { activeLayoutIndex: 0, layouts: [] } },
    }),
  );
  expect(view.result.current).toBeNull();
});
it("hides old behavior IDs while a replacement connection loads", async () => {
  const first = connection(),
    next = connection();
  (call_rpc as jest.Mock).mockImplementation((source, request) =>
    source === next
      ? new Promise(() => {})
      : Promise.resolve(
          request.behaviors.listAllBehaviors
            ? { behaviors: { listAllBehaviors: { behaviors: [1] } } }
            : {
                behaviors: {
                  getBehaviorDetails: { displayName: "synthetic behavior" },
                },
              },
        ),
  );
  const view = renderHook(({ source }) => useBehaviorOptions(source), {
    initialProps: { source: first },
  });
  await waitFor(() => expect(view.result.current).toHaveLength(1));
  view.rerender({ source: next });
  expect(view.result.current).toBeNull();
});
it("does not request old behavior details after a retired list completes", async () => {
  const first = connection(),
    next = connection();
  let finish!: (v: unknown) => void;
  const pending = new Promise((r) => {
    finish = r;
  });
  (call_rpc as jest.Mock).mockImplementation((source) =>
    source === first ? pending : new Promise(() => {}),
  );
  const view = renderHook(({ source }) => useBehaviorOptions(source), {
    initialProps: { source: first },
  });
  await waitFor(() => expect(call_rpc).toHaveBeenCalledTimes(1));
  view.rerender({ source: next });
  await waitFor(() => expect(call_rpc).toHaveBeenCalledTimes(2));
  await act(async () =>
    finish({ behaviors: { listAllBehaviors: { behaviors: [1, 2] } } }),
  );
  expect(call_rpc).toHaveBeenCalledTimes(2);
});
it("contains a synchronous physical-layout transport exception", async () => {
  (call_rpc as jest.Mock).mockImplementation(() => {
    throw new Error("synthetic transport failure");
  });
  const source = connection();
  const view = renderHook(() => usePhysicalLayoutKeys(source));
  await waitFor(() => expect(view.result.current).toBeNull());
});
