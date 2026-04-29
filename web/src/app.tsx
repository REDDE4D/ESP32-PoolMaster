import { LocationProvider, Router, Route } from 'preact-iso';
import { NavShell } from './components/NavShell';
import { Dashboard } from './screens/Dashboard';
import { Manual } from './screens/Manual';
import { Setpoints } from './screens/Setpoints';
import { Calibration } from './screens/Calibration';
import { Pid } from './screens/Pid';
import { Schedule } from './screens/Schedule';
import { Tanks } from './screens/Tanks';
import { History } from './screens/History';
import { Logs } from './screens/Logs';
import { Diagnostics } from './screens/Diagnostics';
import { Settings } from './screens/Settings';
import { Drivers } from './screens/Drivers';
import { Firmware } from './screens/Firmware';

// Placeholder screens — real implementations land in Tasks 17+.
function Placeholder({ name }: { name: string }) {
  return (
    <div class="glass p-6 max-w-xl">
      <div class="label-caps">Screen</div>
      <h1 class="text-xl font-bold mt-1">{name}</h1>
      <p class="text-sm opacity-70 mt-2">Coming in a later SP3 task.</p>
    </div>
  );
}

export function App() {
  return (
    <LocationProvider>
      <NavShell>
        <Router>
          <Route path="/"                      component={Dashboard} />
          <Route path="/control"               component={Manual} />
          <Route path="/control/setpoints"     component={Setpoints} />
          <Route path="/tune"                  component={Calibration} />
          <Route path="/tune/pid"              component={Pid} />
          <Route path="/tune/schedule"         component={Schedule} />
          <Route path="/tune/tanks"            component={Tanks} />
          <Route path="/insights"              component={History} />
          <Route path="/insights/logs"         component={Logs} />
          <Route path="/insights/diagnostics"  component={Diagnostics} />
          <Route path="/settings"              component={Settings} />
          <Route path="/settings/drivers"      component={Drivers} />
          <Route path="/settings/firmware"     component={Firmware} />
          <Route default                       component={() => <Placeholder name="404" />} />
        </Router>
      </NavShell>
    </LocationProvider>
  );
}
