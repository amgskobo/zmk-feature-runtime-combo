import { fireEvent, render, screen, waitFor } from "@testing-library/react";
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

beforeEach(() => jest.resetAllMocks());

it.each([
  ["ASCII boundary", "x".repeat(63), true],
  ["ASCII overflow", "x".repeat(64), false],
  ["UTF-8 boundary", "界".repeat(21), true],
  ["UTF-8 overflow", "界".repeat(22), false],
])(
  "validates the %s name before sending any mutation",
  async (_label, name, accepted) => {
    const mutations: Request[] = [];
    (call_rpc as jest.Mock).mockImplementation((_connection, request) => {
      if (!request.custom) return Promise.resolve({});
      const decoded = Request.decode(request.custom.call.payload);
      let response = Response.create({
        status: { affectedCount: 1, message: "OK" },
      });
      if (decoded.listCombos)
        response = Response.create({ listCombos: { combos: [] } });
      else if (decoded.getGlobalSettings) {
        response = Response.create({
          getGlobalSettings: { settings: { maxCombo: 8 } },
        });
      } else mutations.push(decoded);
      return Promise.resolve({
        custom: { call: { payload: Response.encode(response).finish() } },
      });
    });
    const app = createConnectedMockZMKApp({
      subsystems: [SUBSYSTEM_IDENTIFIER],
    });
    render(
      <ZMKAppProvider value={app}>
        <RPCTestSection />
      </ZMKAppProvider>,
    );
    const save = screen.getByRole("button", { name: "Save Combo" });
    await waitFor(() => expect(save).toBeEnabled());
    fireEvent.change(screen.getByLabelText("Name"), {
      target: { value: name },
    });
    fireEvent.click(save);
    if (accepted) {
      await screen.findByText("Combo saved");
      expect(mutations).toHaveLength(2);
      expect(mutations[0].setCombo).toBeDefined();
      expect(mutations[1].setComboName?.name).toBe(name);
    } else {
      await screen.findByText("Name must fit in 63 UTF-8 bytes");
      expect(mutations).toHaveLength(0);
    }
  },
);
