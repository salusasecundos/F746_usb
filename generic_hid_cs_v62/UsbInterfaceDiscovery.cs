using Microsoft.Win32;
using System;
using System.Collections.Generic;

namespace GenericHid
{
	/// <summary>
	/// Finds every device-interface class that Windows registered for a VID/PID.
	/// This is a fallback for machines where the firmware GUID was cached or the
	/// driver package registered a different WinUSB interface GUID.
	/// </summary>
	internal static class UsbInterfaceDiscovery
	{
		private const String DeviceClassesPath =
			@"SYSTEM\CurrentControlSet\Control\DeviceClasses";
		private const String UsbEnumPath =
			@"SYSTEM\CurrentControlSet\Enum\USB";

		internal static IList<Guid> FindInterfaceGuids(Int32 vendorId, Int32 productId)
		{
			var result = new List<Guid>();
			AddUnique(result, WinUsbDevice.InterfaceGuid);

			String idToken = "vid_" + vendorId.ToString("x4") +
				"&pid_" + productId.ToString("x4");

			FindGuidsInDeviceClasses(idToken, result);
			FindGuidsInUsbEnum(vendorId, productId, result);
			return result;
		}

		private static void FindGuidsInDeviceClasses(
			String idToken,
			IList<Guid> result)
		{
			try
			{
				using (RegistryKey classes = Registry.LocalMachine.OpenSubKey(DeviceClassesPath))
				{
					if (classes == null)
					{
						return;
					}

					foreach (String guidName in classes.GetSubKeyNames())
					{
						Guid interfaceGuid;
						if (!Guid.TryParse(guidName, out interfaceGuid))
						{
							continue;
						}

						using (RegistryKey guidKey = classes.OpenSubKey(guidName))
						{
							if (guidKey == null)
							{
								continue;
							}

							foreach (String interfaceName in guidKey.GetSubKeyNames())
							{
								if (interfaceName.IndexOf(
									idToken,
									StringComparison.OrdinalIgnoreCase) >= 0)
								{
									AddUnique(result, interfaceGuid);
									break;
								}
							}
						}
					}
				}
			}
			catch (UnauthorizedAccessException)
			{
				/* The primary firmware GUID is still tried. */
			}
		}

		private static void FindGuidsInUsbEnum(
			Int32 vendorId,
			Int32 productId,
			IList<Guid> result)
		{
			String hardwareKeyName = "VID_" + vendorId.ToString("X4") +
				"&PID_" + productId.ToString("X4");

			try
			{
				using (RegistryKey usb = Registry.LocalMachine.OpenSubKey(UsbEnumPath))
				using (RegistryKey hardware = usb == null ? null : usb.OpenSubKey(hardwareKeyName))
				{
					if (hardware == null)
					{
						return;
					}

					foreach (String instanceName in hardware.GetSubKeyNames())
					{
						using (RegistryKey parameters = hardware.OpenSubKey(
							instanceName + @"\Device Parameters"))
						{
							if (parameters == null)
							{
								continue;
							}

							AddGuidsFromValue(parameters.GetValue("DeviceInterfaceGUID"), result);
							AddGuidsFromValue(parameters.GetValue("DeviceInterfaceGUIDs"), result);
						}
					}
				}
			}
			catch (UnauthorizedAccessException)
			{
				/* The DeviceClasses scan and primary GUID are still available. */
			}
		}

		private static void AddGuidsFromValue(Object value, IList<Guid> result)
		{
			var single = value as String;
			if (single != null)
			{
				AddGuidString(single, result);
				return;
			}

			var multiple = value as String[];
			if (multiple == null)
			{
				return;
			}

			foreach (String text in multiple)
			{
				AddGuidString(text, result);
			}
		}

		private static void AddGuidString(String text, IList<Guid> result)
		{
			Guid guid;
			if (Guid.TryParse(text, out guid))
			{
				AddUnique(result, guid);
			}
		}

		private static void AddUnique(IList<Guid> result, Guid guid)
		{
			if (!result.Contains(guid))
			{
				result.Add(guid);
			}
		}
	}
}
