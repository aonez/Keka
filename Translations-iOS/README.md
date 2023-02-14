Any trouble just get in touch in the [Issues](https://github.com/aonez/Keka/issues) section.

Thanks a lot for your contribution! 😃

## Keka for iOS translation

Note this version is not available for free. You can ask for a code to review it or download the beta version using the TestFlight link available at the [iOS web](https://ios.keka.io).

- Fork or download this project.
- Choose your preferred `xcloc` language ([en.xcloc](en.xcloc) is the base English version).
- *RECOMMENDED*: You can open the file with Xcode and translate there all the available strings.
- If you prefer to do it without Xcode you can also open the `xcloc` bundle (Right Click - Show Package Contents) and:
  - Locate the `.xliff` file inside `Localized Contents` and open it with your prefered editor.
  - For every `trans-unit` node, translate the `source` into the `target` node. If no `target` exists yet, just create the line.<br />
    Do not translate the `source`, `note` or any other node except for the `target` one.<br /><br />
    ```xml
    <trans-unit id="COMPRESSION_RUNNING" xml:space="preserve">
      <source>Compressing</source>
      <target>Comprimiendo</target>
      <note>Compressing, operation running.</note>
    </trans-unit>
    ```
- Alternatively you can use the [Alternative-Strings-Files](Alternative-Strings-Files) contents. Just duplicate the `lproj` folder with your language and translate the right side from the strings pairs in all the `.strings` files.

To upload your translation create a pull request or send it via mail.
